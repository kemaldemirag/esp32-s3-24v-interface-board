"""CF-09 HYB-REV10 deterministic host model.

This is an ASSUMPTION / MODEL_ONLY test harness.  It is not an ESP-IDF
driver, a wire protocol, a timing proof, or evidence for an electrical PASS.
CRC-32 is used only as a test-envelope oracle; it is not a product decision.

The model verifies topology, queue ordering, explicit backpressure, output
interlocks, accepted-vs-model-serializer-complete separation, and fault visibility.
Real SPI/FIFO/IRQ semantics, UART timing, boot waveforms, RMT behaviour and
all electrical properties remain HARDWARE_REQUIRED / NOT_RUN.
"""

from __future__ import annotations

import argparse
import binascii
import hashlib
import json
import struct
from collections import Counter, defaultdict, deque
from dataclasses import asdict, dataclass
from enum import Enum
from typing import Deque, Iterable


MODEL_CLASS = "ASSUMPTION / MODEL_ONLY"
CRC_CLASS = "ASSUMPTION / TEST_ENVELOPE_CRC32"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"
HYBRID_SELECTION_POLICY = (
    "ASSUMPTION / MODEL_ONLY: override_pairs explicitly selects any subset of RS232 "
    "pair indices 0..4; all 32 subsets are modelled, not frozen as PCB net assignments"
)


class CommitResult(str, Enum):
    """Synthetic host-transaction result."""

    NOT_COMMITTED = "NOT_COMMITTED"
    COMMITTED = "COMMITTED"
    UNKNOWN = "UNKNOWN"


class Bank(str, Enum):
    TTL = "TTL"
    RS232 = "RS232"


class BankState(str, Enum):
    SAFE_DISABLED = "SAFE_DISABLED"
    CONFIGURING = "CONFIGURING"
    IDLE_CONFIRMED = "IDLE_CONFIRMED"
    ENABLED = "ENABLED"


class RxDecision(str, Enum):
    FREE_BRIDGE_A_CHANNEL = "FREE_BRIDGE_A_CHANNEL"
    SHARED_RX_ALLOWED = "SHARED_RX_ALLOWED"
    NEEDS_DEDICATED_RX = "NEEDS_DEDICATED_RX"


@dataclass(frozen=True)
class Topology:
    """HYB-REV10 topology with an explicit independently driven RS-232 subset."""

    override_pairs: frozenset[int]

    def __post_init__(self) -> None:
        normalized = frozenset(self.override_pairs)
        if any(not isinstance(index, int) or not 0 <= index <= 4 for index in normalized):
            raise ValueError("override_pairs must contain only integer pair indices 0..4")
        object.__setattr__(self, "override_pairs", normalized)

    @classmethod
    def mirrored(cls) -> "Topology":
        return cls(frozenset())

    @classmethod
    def independent(cls) -> "Topology":
        return cls(frozenset(range(5)))

    @classmethod
    def canonical_prefix(cls, count: int) -> "Topology":
        """Convenience for a prefix sensitivity only; not a physical-selection rule."""

        if not 0 <= count <= 5:
            raise ValueError("count must be in the closed range 0..5")
        return cls(frozenset(range(count)))

    @property
    def independent_overrides(self) -> int:
        return len(self.override_pairs)

    @property
    def label(self) -> str:
        if not self.override_pairs:
            return "MIRRORED_5"
        if self.override_pairs == frozenset(range(5)):
            return "INDEPENDENT_10"
        suffix = "_".join(str(index) for index in sorted(self.override_pairs))
        return f"HYBRID_PAIRS_{suffix}"

    @property
    def logical_channels(self) -> tuple[int, ...]:
        return tuple(range(5)) + tuple(5 + index for index in sorted(self.override_pairs))

    @property
    def logical_channel_count(self) -> int:
        return len(self.logical_channels)


@dataclass(frozen=True)
class Route:
    logical_channel: int
    serializer: str
    resource: str
    bridge: str | None
    bridge_channel: int | None
    endpoints: tuple[str, ...]


def _serializer_descriptor(index: int) -> tuple[str, str, str | None, int | None]:
    if index == 0:
        return "S0", "UART1", None, None
    if index == 1:
        return "S1", "UART2", None, None
    if 2 <= index <= 5:
        channel = index - 2
        return f"S{index}", f"BRIDGE_A_CH{channel}", "A", channel
    if 6 <= index <= 9:
        channel = index - 6
        return f"S{index}", f"BRIDGE_B_CH{channel}", "B", channel
    raise ValueError(f"serializer index outside S0..S9: {index}")


def build_routes(topology: Topology) -> dict[int, Route]:
    """Build HYB-REV10 routes for the explicitly selected override subset."""

    routes: dict[int, Route] = {}
    override_pairs = topology.override_pairs

    # L0..L4 always feed TTL0..TTL4.  The RS-232 leg stays on the same
    # serializer unless that pair is selected as an independent override.
    for logical_channel in range(5):
        endpoints = [f"TTL{logical_channel}"]
        if logical_channel not in override_pairs:
            endpoints.append(f"RS232{logical_channel}")
        serializer, resource, bridge, bridge_channel = _serializer_descriptor(logical_channel)
        routes[logical_channel] = Route(
            logical_channel=logical_channel,
            serializer=serializer,
            resource=resource,
            bridge=bridge,
            bridge_channel=bridge_channel,
            endpoints=tuple(endpoints),
        )

    # L(5+i) has a stable identity and independently feeds RS232_i.
    for override_index in sorted(override_pairs):
        logical_channel = 5 + override_index
        serializer_index = 5 + override_index
        serializer, resource, bridge, bridge_channel = _serializer_descriptor(serializer_index)
        routes[logical_channel] = Route(
            logical_channel=logical_channel,
            serializer=serializer,
            resource=resource,
            bridge=bridge,
            bridge_channel=bridge_channel,
            endpoints=(f"RS232{override_index}",),
        )

    physical_endpoints = {endpoint for route in routes.values() for endpoint in route.endpoints}
    expected_endpoints = {f"TTL{i}" for i in range(5)} | {f"RS232{i}" for i in range(5)}
    if physical_endpoints != expected_endpoints:
        raise AssertionError("routing table does not cover exactly the ten CF-09 TX endpoints")
    if len({route.serializer for route in routes.values()}) != len(routes):
        raise AssertionError("two logical channels unexpectedly share a serializer")
    return routes


@dataclass(frozen=True)
class Frame:
    epoch: int
    logical_channel: int
    sequence: int
    payload: bytes
    test_crc32: int

    @classmethod
    def create(cls, epoch: int, logical_channel: int, sequence: int, payload: bytes) -> "Frame":
        payload_bytes = bytes(payload)
        test_crc32 = cls.compute_crc(epoch, logical_channel, sequence, payload_bytes)
        return cls(epoch, logical_channel, sequence, payload_bytes, test_crc32)

    @staticmethod
    def compute_crc(epoch: int, logical_channel: int, sequence: int, payload: bytes) -> int:
        envelope = struct.pack(">III", epoch, logical_channel, sequence) + bytes(payload)
        return binascii.crc32(envelope) & 0xFFFFFFFF

    def crc_is_valid(self) -> bool:
        return self.test_crc32 == self.compute_crc(
            self.epoch, self.logical_channel, self.sequence, self.payload
        )


@dataclass(frozen=True)
class Observation:
    endpoint: str
    serializer: str
    epoch: int
    logical_channel: int
    sequence: int
    payload: bytes
    test_crc32: int

    @classmethod
    def from_frame(cls, endpoint: str, serializer: str, frame: Frame) -> "Observation":
        return cls(
            endpoint=endpoint,
            serializer=serializer,
            epoch=frame.epoch,
            logical_channel=frame.logical_channel,
            sequence=frame.sequence,
            payload=frame.payload,
            test_crc32=frame.test_crc32,
        )

    def crc_is_valid(self) -> bool:
        return self.test_crc32 == Frame.compute_crc(
            self.epoch, self.logical_channel, self.sequence, self.payload
        )


@dataclass(frozen=True)
class LedgerEvent:
    tick: int
    kind: str
    topology: str
    epoch: int | None = None
    logical_channel: int | None = None
    sequence: int | None = None
    serializer: str | None = None
    endpoints: tuple[str, ...] = ()
    detail: str = ""


def endpoint_bank(endpoint: str) -> Bank:
    if endpoint.startswith("TTL"):
        return Bank.TTL
    if endpoint.startswith("RS232"):
        return Bank.RS232
    raise ValueError(f"unknown endpoint: {endpoint}")


class HostModel:
    """Deterministic queue, route, interlock and fault-visibility model."""

    _ALLOWED_BANK_TRANSITIONS = {
        BankState.SAFE_DISABLED: {BankState.CONFIGURING},
        BankState.CONFIGURING: {BankState.IDLE_CONFIRMED, BankState.SAFE_DISABLED},
        BankState.IDLE_CONFIRMED: {BankState.ENABLED, BankState.SAFE_DISABLED},
        BankState.ENABLED: {BankState.SAFE_DISABLED},
    }

    def __init__(
        self,
        topology: Topology,
        *,
        queue_limit: int | None = None,
        epoch: int = 1,
    ) -> None:
        if queue_limit is not None and queue_limit < 1:
            raise ValueError("queue_limit must be positive or None")
        if epoch < 0:
            raise ValueError("epoch must be non-negative")

        self.topology = topology
        self.routes = build_routes(topology)
        self.channel_order = tuple(sorted(self.routes))
        self.queue_limit = queue_limit
        self.epoch = epoch
        self.queues: dict[int, Deque[Frame]] = {
            channel: deque() for channel in self.channel_order
        }
        self.next_sequence = {channel: 0 for channel in self.queues}
        self.bank_states = {
            Bank.TTL: BankState.SAFE_DISABLED,
            Bank.RS232: BankState.SAFE_DISABLED,
        }
        self.bridge_up = {"A": True, "B": True}
        self.required_resources = {route.resource for route in self.routes.values()}
        self.configured_resources: set[str] = set()
        self.idle_confirmed_resources: set[str] = set()
        self.failed_endpoints: set[str] = set()
        self.inflight: dict[str, Frame] = {}
        self.accepted_frames: list[Frame] = []
        self.uncertain: list[Frame] = []
        self.observations: list[Observation] = []
        self.ledger: list[LedgerEvent] = []
        self._tick = 0
        self._round_robin_cursor = 0
        self._reported_blocks: set[tuple[str, int, int, int]] = set()

    def _record(
        self,
        kind: str,
        *,
        frame: Frame | None = None,
        route: Route | None = None,
        endpoints: tuple[str, ...] = (),
        detail: str = "",
    ) -> LedgerEvent:
        self._tick += 1
        event = LedgerEvent(
            tick=self._tick,
            kind=kind,
            topology=self.topology.label,
            epoch=frame.epoch if frame else self.epoch,
            logical_channel=frame.logical_channel if frame else None,
            sequence=frame.sequence if frame else None,
            serializer=route.serializer if route else None,
            endpoints=endpoints,
            detail=detail,
        )
        self.ledger.append(event)
        return event

    def transition_bank(self, bank: Bank, target: BankState) -> None:
        current = self.bank_states[bank]
        if target not in self._ALLOWED_BANK_TRANSITIONS[current]:
            raise ValueError(f"illegal bank transition: {bank.value} {current.value} -> {target.value}")
        self.bank_states[bank] = target
        self._record("BANK_STATE", detail=f"{bank.value}:{current.value}->{target.value}")
        if target == BankState.SAFE_DISABLED:
            self._mark_inflight_uncertain(
                lambda route: any(endpoint_bank(endpoint) == bank for endpoint in route.endpoints),
                detail=f"bank_{bank.value}_disabled_after_accept;no_auto_retry",
            )

    def arm_outputs(self) -> None:
        """Enable both banks only after explicit model resource readiness."""

        missing_config = self.required_resources - self.configured_resources
        missing_idle = self.required_resources - self.idle_confirmed_resources
        down_bridges = {
            route.bridge
            for route in self.routes.values()
            if route.bridge is not None and not self.bridge_up[route.bridge]
        }
        if missing_config or missing_idle or down_bridges or self.failed_endpoints:
            raise RuntimeError(
                "output arm preconditions not met: "
                f"missing_config={sorted(missing_config)};"
                f"missing_idle={sorted(missing_idle)};"
                f"down_bridges={sorted(down_bridges)};"
                f"failed_endpoints={sorted(self.failed_endpoints)}"
            )

        non_safe_banks = {
            bank.value: state.value
            for bank, state in self.bank_states.items()
            if state != BankState.SAFE_DISABLED
        }
        if non_safe_banks:
            raise ValueError(f"all banks must start SAFE_DISABLED: {non_safe_banks}")

        for bank in (Bank.TTL, Bank.RS232):
            self.transition_bank(bank, BankState.CONFIGURING)
            self.transition_bank(bank, BankState.IDLE_CONFIRMED)
            self.transition_bank(bank, BankState.ENABLED)

    def declare_resources_ready(self, resources: Iterable[str] | None = None) -> None:
        """Declare synthetic config and idle checks; this is not hardware evidence."""

        selected = self.required_resources if resources is None else set(resources)
        unknown = selected - self.required_resources
        if unknown:
            raise ValueError(f"resources outside topology: {sorted(unknown)}")

        for resource in sorted(selected):
            route = next(route for route in self.routes.values() if route.resource == resource)
            if route.bridge is not None and not self.bridge_up[route.bridge]:
                raise RuntimeError(f"cannot declare {resource} ready while bridge {route.bridge} is down")
            self.configured_resources.add(resource)
            self._record("RESOURCE_CONFIGURED", route=route, detail="MODEL_ONLY")
            self.idle_confirmed_resources.add(resource)
            self._record("RESOURCE_IDLE_CONFIRMED", route=route, detail="MODEL_ONLY")

    def _mark_inflight_uncertain(self, predicate, *, detail: str) -> None:
        for serializer, frame in list(self.inflight.items()):
            route = self.routes[frame.logical_channel]
            if not predicate(route):
                continue
            del self.inflight[serializer]
            self.uncertain.append(frame)
            self._record("UNCERTAIN", frame=frame, route=route, detail=detail)

    def force_safe_disabled(self, reason: str) -> None:
        # Record and apply disable before classifying accepted in-flight frames.
        for bank in (Bank.TTL, Bank.RS232):
            previous = self.bank_states[bank]
            self.bank_states[bank] = BankState.SAFE_DISABLED
            self._record(
                "BANK_STATE",
                detail=f"{bank.value}:{previous.value}->SAFE_DISABLED;reason={reason}",
            )
        self._mark_inflight_uncertain(
            lambda _route: True,
            detail=f"safe_disable_after_accept;reason={reason};no_auto_retry",
        )

    def enqueue(self, logical_channel: int, payload: bytes) -> Frame | None:
        if logical_channel not in self.queues:
            raise ValueError(f"logical channel outside topology: {logical_channel}")
        queue = self.queues[logical_channel]
        sequence = self.next_sequence[logical_channel]
        if self.queue_limit is not None and len(queue) >= self.queue_limit:
            rejected = Frame.create(self.epoch, logical_channel, sequence, payload)
            self._record(
                "BACKPRESSURE",
                frame=rejected,
                route=self.routes[logical_channel],
                detail=f"queue_limit={self.queue_limit};silent_drop=false",
            )
            return None

        frame = Frame.create(self.epoch, logical_channel, sequence, payload)
        queue.append(frame)
        self.next_sequence[logical_channel] += 1
        self._record("ENQUEUED", frame=frame, route=self.routes[logical_channel])
        return frame

    def pending_count(self) -> int:
        return sum(len(queue) for queue in self.queues.values())

    def _route_ready(self, route: Route, frame: Frame) -> bool:
        if (
            route.resource not in self.configured_resources
            or route.resource not in self.idle_confirmed_resources
        ):
            return False
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            return False
        if route.serializer in self.inflight:
            return False
        if any(self.bank_states[endpoint_bank(endpoint)] != BankState.ENABLED for endpoint in route.endpoints):
            return False

        failed = tuple(endpoint for endpoint in route.endpoints if endpoint in self.failed_endpoints)
        if failed:
            kind = "PAIR_DEGRADED" if len(route.endpoints) > 1 else "OUTPUT_BLOCKED"
            detail = (
                "STOP_PAIR;silent_degraded_delivery=false"
                if len(route.endpoints) > 1
                else "BLOCK_CHANNEL;silent_delivery=false"
            )
            key = (kind, frame.epoch, frame.logical_channel, frame.sequence)
            if key not in self._reported_blocks:
                self._reported_blocks.add(key)
                self._record(
                    kind,
                    frame=frame,
                    route=route,
                    endpoints=failed,
                    detail=detail,
                )
            return False
        return True

    def _select_channel(self) -> int | None:
        count = len(self.channel_order)
        for offset in range(count):
            position = (self._round_robin_cursor + offset) % count
            channel = self.channel_order[position]
            queue = self.queues[channel]
            if not queue:
                continue
            frame = queue[0]
            if self._route_ready(self.routes[channel], frame):
                self._round_robin_cursor = (position + 1) % count
                return channel
        return None

    def service_one(
        self,
        logical_channel: int | None = None,
        *,
        commit_result: CommitResult = CommitResult.COMMITTED,
        serializer_complete: bool = True,
    ) -> bool:
        if logical_channel is None:
            logical_channel = self._select_channel()
            if logical_channel is None:
                return False
        elif logical_channel not in self.queues:
            raise ValueError(f"logical channel outside topology: {logical_channel}")

        queue = self.queues[logical_channel]
        if not queue:
            return False
        frame = queue[0]
        route = self.routes[logical_channel]
        if not self._route_ready(route, frame):
            return False

        if commit_result == CommitResult.NOT_COMMITTED:
            self._record(
                "TRANSACTION_NOT_COMMITTED",
                frame=frame,
                route=route,
                detail="frame_retained;retry_is_safe_in_model",
            )
            return True

        queue.popleft()
        if commit_result == CommitResult.UNKNOWN:
            self.uncertain.append(frame)
            self._record(
                "UNCERTAIN",
                frame=frame,
                route=route,
                detail="commit_unknown;no_auto_retry",
            )
            return True

        self.accepted_frames.append(frame)
        self._record("ACCEPTED_BY_RESOURCE", frame=frame, route=route)
        if serializer_complete:
            self._complete_serializer_model(route, frame)
        else:
            self.inflight[route.serializer] = frame
        return True

    def _complete_serializer_model(self, route: Route, frame: Frame) -> None:
        self._record(
            "MODEL_SERIALIZER_COMPLETE",
            frame=frame,
            route=route,
            endpoints=route.endpoints,
            detail="synthetic_endpoint_expectation;not_connector_measurement",
        )
        self.observations.extend(
            Observation.from_frame(endpoint, route.serializer, frame) for endpoint in route.endpoints
        )

    def complete_serializer(self, serializer: str) -> bool:
        frame = self.inflight.pop(serializer, None)
        if frame is None:
            return False
        route = self.routes[frame.logical_channel]
        unsafe_reasons: list[str] = []
        if route.resource not in self.configured_resources:
            unsafe_reasons.append("resource_not_configured")
        if route.resource not in self.idle_confirmed_resources:
            unsafe_reasons.append("resource_idle_not_confirmed")
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            unsafe_reasons.append(f"bridge_{route.bridge}_down")
        if any(self.bank_states[endpoint_bank(endpoint)] != BankState.ENABLED for endpoint in route.endpoints):
            unsafe_reasons.append("bank_not_enabled")
        failed = sorted(endpoint for endpoint in route.endpoints if endpoint in self.failed_endpoints)
        if failed:
            unsafe_reasons.append(f"endpoint_fault={','.join(failed)}")
        if unsafe_reasons:
            self.uncertain.append(frame)
            self._record(
                "UNCERTAIN",
                frame=frame,
                route=route,
                detail="completion_preconditions_failed;"
                + ";".join(unsafe_reasons)
                + ";no_auto_retry",
            )
            return False
        self._complete_serializer_model(route, frame)
        return True

    def run_until_stalled(self, *, max_services: int = 100_000) -> int:
        serviced = 0
        while serviced < max_services and self.service_one():
            serviced += 1
        if serviced == max_services and self.pending_count() > 0:
            raise RuntimeError("model service limit reached before queues stalled")
        return serviced

    def set_bridge(self, bridge: str, *, up: bool, reason: str) -> None:
        if bridge not in self.bridge_up:
            raise ValueError("bridge must be A or B")
        self.bridge_up[bridge] = up
        bridge_detail = f"bridge={bridge};reason={reason}"
        if not up:
            bridge_detail += ";physical_output_safety=UNKNOWN_HARDWARE_REQUIRED"
        self._record("BRIDGE_UP" if up else "BRIDGE_DOWN", detail=bridge_detail)
        if up:
            return

        affected_resources = {
            route.resource for route in self.routes.values() if route.bridge == bridge
        }
        self.configured_resources.difference_update(affected_resources)
        self.idle_confirmed_resources.difference_update(affected_resources)
        self._mark_inflight_uncertain(
            lambda route: route.bridge == bridge,
            detail="accepted_not_complete_during_bridge_fault;no_auto_retry",
        )

    def inject_endpoint_fault(self, endpoint: str) -> None:
        known = {endpoint for route in self.routes.values() for endpoint in route.endpoints}
        if endpoint not in known:
            raise ValueError(f"endpoint outside topology: {endpoint}")
        self.failed_endpoints.add(endpoint)
        self._record("ENDPOINT_FAULT_INJECTED", endpoints=(endpoint,))
        self._mark_inflight_uncertain(
            lambda route: endpoint in route.endpoints,
            detail=f"accepted_not_complete_during_endpoint_fault={endpoint};no_auto_retry",
        )

    def clear_endpoint_fault(self, endpoint: str) -> None:
        self.failed_endpoints.discard(endpoint)
        self._record("ENDPOINT_FAULT_CLEARED", endpoints=(endpoint,))

    def restart(self, new_epoch: int, *, reason: str) -> None:
        """Model-only host restart policy: expose uncertainty and discard stale queues."""

        if new_epoch <= self.epoch:
            raise ValueError("new_epoch must be greater than the current epoch")

        # Safety action is first in the restart ledger and exposes in-flight uncertainty.
        self.force_safe_disabled(reason)

        for channel, queue in self.queues.items():
            route = self.routes[channel]
            while queue:
                frame = queue.popleft()
                self._record(
                    "STALE_QUEUE_DISCARDED",
                    frame=frame,
                    route=route,
                    detail="explicit_model_policy;silent_replay=false",
                )

        self.configured_resources.clear()
        self.idle_confirmed_resources.clear()
        old_epoch = self.epoch
        self.epoch = new_epoch
        self.next_sequence = {channel: 0 for channel in self.queues}
        self._round_robin_cursor = 0
        self._record("RESTART", detail=f"epoch={old_epoch}->{new_epoch};reason={reason}")

    def uncertain_ranges(self) -> list[dict[str, int]]:
        grouped: dict[tuple[int, int], set[int]] = defaultdict(set)
        for frame in self.uncertain:
            grouped[(frame.epoch, frame.logical_channel)].add(frame.sequence)

        ranges: list[dict[str, int]] = []
        for (epoch, channel), values in sorted(grouped.items()):
            ordered = sorted(values)
            start = previous = ordered[0]
            for value in ordered[1:]:
                if value == previous + 1:
                    previous = value
                    continue
                ranges.append(
                    {"epoch": epoch, "logical_channel": channel, "start": start, "end": previous}
                )
                start = previous = value
            ranges.append(
                {"epoch": epoch, "logical_channel": channel, "start": start, "end": previous}
            )
        return ranges

    def expected_observations(self) -> list[Observation]:
        """Return the exact endpoint manifest implied by committed frames."""

        expected: list[Observation] = []
        for frame in self.accepted_frames:
            route = self.routes[frame.logical_channel]
            expected.extend(
                Observation.from_frame(endpoint, route.serializer, frame)
                for endpoint in route.endpoints
            )
        return expected

    def ledger_digest(self) -> str:
        canonical = json.dumps(
            [asdict(event) for event in self.ledger],
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
        return hashlib.sha256(canonical).hexdigest().upper()


class ReceiverOracle:
    """Compare observations to an explicit manifest and detect ordering errors."""

    def __init__(self, topology: Topology) -> None:
        self.topology = topology
        self.expected_channel_by_endpoint = {
            endpoint: channel
            for channel, route in build_routes(topology).items()
            for endpoint in route.endpoints
        }

    @staticmethod
    def _key(observation: Observation) -> tuple[object, ...]:
        return (
            observation.endpoint,
            observation.serializer,
            observation.epoch,
            observation.logical_channel,
            observation.sequence,
            observation.payload,
            observation.test_crc32,
        )

    def validate(
        self,
        observations: Iterable[Observation],
        expected_observations: Iterable[Observation],
    ) -> list[str]:
        observed_list = list(observations)
        expected_list = list(expected_observations)
        anomalies: list[str] = []
        observed_counts = Counter(self._key(item) for item in observed_list)
        expected_counts = Counter(self._key(item) for item in expected_list)

        exemplar = {self._key(item): item for item in expected_list + observed_list}
        for key, expected_count in sorted(expected_counts.items(), key=lambda item: repr(item[0])):
            missing_count = expected_count - observed_counts[key]
            if missing_count <= 0:
                continue
            item = exemplar[key]
            anomalies.append(
                f"MISSING_EXPECTED:{item.endpoint}:serializer={item.serializer}:"
                f"epoch={item.epoch}:channel={item.logical_channel}:seq={item.sequence}:"
                f"count={missing_count}"
            )
        for key, observed_count in sorted(observed_counts.items(), key=lambda item: repr(item[0])):
            unexpected_count = observed_count - expected_counts[key]
            if unexpected_count <= 0:
                continue
            item = exemplar[key]
            anomalies.append(
                f"UNEXPECTED_OBSERVATION:{item.endpoint}:serializer={item.serializer}:"
                f"epoch={item.epoch}:channel={item.logical_channel}:seq={item.sequence}:"
                f"count={unexpected_count}"
            )

        expected_next: dict[tuple[str, int], int] = defaultdict(int)
        seen: dict[tuple[str, int], set[int]] = defaultdict(set)

        for observation in observed_list:
            expected_channel = self.expected_channel_by_endpoint.get(observation.endpoint)
            if expected_channel is None:
                anomalies.append(f"UNKNOWN_ENDPOINT:{observation.endpoint}")
            elif expected_channel != observation.logical_channel:
                anomalies.append(
                    f"WRONG_CHANNEL:{observation.endpoint}:expected={expected_channel}:"
                    f"observed={observation.logical_channel}"
                )

            if not observation.crc_is_valid():
                anomalies.append(
                    f"CRC_MISMATCH:{observation.endpoint}:epoch={observation.epoch}:"
                    f"seq={observation.sequence}"
                )

            key = (observation.endpoint, observation.epoch)
            if observation.sequence in seen[key]:
                anomalies.append(
                    f"DUPLICATE:{observation.endpoint}:epoch={observation.epoch}:"
                    f"seq={observation.sequence}"
                )
                continue

            expected = expected_next[key]
            if observation.sequence > expected:
                anomalies.append(
                    f"MISSING:{observation.endpoint}:epoch={observation.epoch}:"
                    f"start={expected}:end={observation.sequence - 1}"
                )
            elif observation.sequence < expected:
                anomalies.append(
                    f"REORDER:{observation.endpoint}:epoch={observation.epoch}:"
                    f"seq={observation.sequence}:expected_at_least={expected}"
                )

            seen[key].add(observation.sequence)
            expected_next[key] = max(expected, observation.sequence + 1)
        return anomalies


def shared_rx_decision(
    topology: Topology,
    *,
    assume_shared_rx: bool,
    rx_config_token: str,
    s5_tx_config_token: str | None,
) -> RxDecision:
    """Evaluate only the declared S5.RX configuration-sharing assumption."""

    if 0 not in topology.override_pairs:
        return RxDecision.FREE_BRIDGE_A_CHANNEL
    if (
        assume_shared_rx
        and s5_tx_config_token is not None
        and rx_config_token == s5_tx_config_token
    ):
        return RxDecision.SHARED_RX_ALLOWED
    return RxDecision.NEEDS_DEDICATED_RX


def run_demo(
    override_pairs: Iterable[int],
    frames_per_channel: int,
    *,
    assume_shared_rx: bool | None = None,
    rx_config_token: str | None = None,
    s5_tx_config_token: str | None = None,
) -> dict[str, object]:
    if frames_per_channel < 1:
        raise ValueError("frames_per_channel must be positive")
    topology = Topology(frozenset(override_pairs))
    model = HostModel(topology)
    model.declare_resources_ready()
    model.arm_outputs()

    for frame_index in range(frames_per_channel):
        for channel in topology.logical_channels:
            payload = f"MODEL_ONLY:{topology.label}:L{channel}:F{frame_index}".encode("ascii")
            model.enqueue(channel, payload)
    serviced = model.run_until_stalled()
    anomalies = ReceiverOracle(topology).validate(
        model.observations,
        model.expected_observations(),
    )

    if 0 not in topology.override_pairs:
        rx_decision: str = RxDecision.FREE_BRIDGE_A_CHANNEL.value
    elif assume_shared_rx is False:
        rx_decision = RxDecision.NEEDS_DEDICATED_RX.value
    elif assume_shared_rx is None or rx_config_token is None:
        rx_decision = "NOT_EVALUATED"
    else:
        rx_decision = shared_rx_decision(
            topology,
            assume_shared_rx=assume_shared_rx,
            rx_config_token=rx_config_token,
            s5_tx_config_token=s5_tx_config_token,
        ).value

    tx_model_pass = (
        not anomalies
        and model.pending_count() == 0
        and not model.inflight
        and not model.uncertain
    )

    return {
        "status": "TX_HOST_MODEL_PASS" if tx_model_pass else "TX_HOST_MODEL_FAIL",
        "model_class": MODEL_CLASS,
        "crc_class": CRC_CLASS,
        "hybrid_selection_policy": HYBRID_SELECTION_POLICY,
        "model_pass_scope": "TX topology/order/interlock/fault-visibility only",
        "hardware_status": HARDWARE_STATUS,
        "topology": topology.label,
        "override_pairs": sorted(topology.override_pairs),
        "independent_overrides": topology.independent_overrides,
        "logical_channel_count": topology.logical_channel_count,
        "logical_channel_ids": list(topology.logical_channels),
        "physical_tx_endpoints": 10,
        "frames_per_channel": frames_per_channel,
        "service_operations": serviced,
        "accepted_frames": len(model.accepted_frames),
        "model_endpoint_observations": len(model.observations),
        "pending_frames": model.pending_count(),
        "uncertain_ranges": model.uncertain_ranges(),
        "anomalies": anomalies,
        "rx_decision": rx_decision,
        "unknown_late_completion_status": "NOT_MODELED / UNKNOWN",
        "epoch_source_status": "CALLER_SUPPLIED / PERSISTENCE_UNKNOWN",
        "fault_telemetry_status": "MODEL_ONLY_INJECTION / PHYSICAL_DETECTION_UNKNOWN",
        "bridge_reset_output_safety": "UNKNOWN / HARDWARE_REQUIRED",
        "ledger_sha256": model.ledger_digest(),
        "routes": [asdict(model.routes[channel]) for channel in sorted(model.routes)],
    }


def parse_override_pairs(value: str) -> frozenset[int]:
    normalized = value.strip().lower()
    if not normalized:
        return frozenset()
    if normalized == "all":
        return frozenset(range(5))
    try:
        result = frozenset(int(item.strip()) for item in normalized.split(",") if item.strip())
        return Topology(result).override_pairs
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--override-pairs",
        type=parse_override_pairs,
        default=frozenset(),
        metavar="PAIR[,PAIR...]|all",
        help="MODEL_ONLY independently driven RS-232 pair indices 0..4; empty=MIRRORED_5",
    )
    parser.add_argument(
        "--frames-per-channel",
        type=int,
        default=2,
        help="synthetic frame count per logical channel",
    )
    args = parser.parse_args()
    result = run_demo(args.override_pairs, args.frames_per_channel)
    print(json.dumps(result, indent=2, ensure_ascii=False))
    return 0 if result["status"] == "TX_HOST_MODEL_PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
