"""Token-safe CF-09 transport integration seam.

This module is ASSUMPTION / MODEL_ONLY.  It turns the topology and frame types
from ``cf09_host_model_v1`` into an explicit asynchronous transport contract.
Commit and completion callbacks are accepted only when their immutable token
matches the caller-supplied boot/session identity, frame, attempt, serializer,
resource and current resource generation.  A late callback therefore cannot
complete a newer frame that reuses the same serializer.  The source and
persistence of the required session identity remain an integration decision.
The caller MUST keep it unique and non-reused for the maximum late-callback
lifetime; this model validates non-emptiness but cannot invent that identity.

The adapter models scheduler and recovery semantics only.  A COMPLETE event is
model serializer completion, not proof of connector-level delivery.  SPI/FIFO/
IRQ timing, real output-enable behaviour and all electrical properties remain
HARDWARE_REQUIRED / NOT_RUN.

Calls must be serialized by one owner or an external critical section.  A
fail-fast guard rejects concurrent or reentrant public mutations in this
Python model; it is not evidence that a firmware/ISR integration is safe.
"""

from __future__ import annotations

import hashlib
import json
from collections import defaultdict, deque
from dataclasses import asdict, dataclass
from enum import Enum
from functools import wraps
from threading import Lock
from typing import Deque, Iterable

from cf09_host_model_v1 import (
    Bank,
    BankState,
    CommitResult,
    Frame,
    Observation,
    ReceiverOracle,
    Route,
    Topology,
    build_routes,
    endpoint_bank,
)


MODEL_CLASS = "ASSUMPTION / MODEL_ONLY"
COMPLETION_SCOPE = "MODEL_SERIALIZER_COMPLETE / NOT_CONNECTOR_DELIVERY"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"
SESSION_ID_STATUS = (
    "CALLER_SUPPLIED / UNIQUE_AND_NON_REUSED_FOR_MAX_LATE_CALLBACK_LIFETIME"
)
CONCURRENCY_CONTRACT = "SINGLE_WRITER_OR_EXTERNAL_CRITICAL_SECTION_REQUIRED"
U32_MAX = (1 << 32) - 1
ENVELOPE_INTEGER_DOMAIN = "DERIVED_FROM_FRAME_COMPUTE_CRC_>III_UNSIGNED_32_BIT"


def _serialized_mutation(method):
    """Reject concurrent/reentrant mutations instead of interleaving state."""

    @wraps(method)
    def guarded(self, *args, **kwargs):
        if not self._mutation_lock.acquire(blocking=False):
            active = self._active_mutation or "another_thread"
            raise RuntimeError(
                f"concurrent/reentrant mutation rejected: active={active};"
                f"requested={method.__name__}"
            )
        self._active_mutation = method.__name__
        try:
            return method(self, *args, **kwargs)
        finally:
            self._active_mutation = None
            self._mutation_lock.release()

    return guarded


class EnqueueStatus(str, Enum):
    ENQUEUED = "ENQUEUED"
    BACKPRESSURE = "BACKPRESSURE"


class AttemptState(str, Enum):
    COMMIT_PENDING = "COMMIT_PENDING"
    NOT_COMMITTED = "NOT_COMMITTED"
    INFLIGHT = "INFLIGHT"
    COMPLETE = "COMPLETE"
    UNCERTAIN = "UNCERTAIN"


class StaleResolution(str, Enum):
    REENQUEUED_AS_NEW_FRAME = "REENQUEUED_AS_NEW_FRAME"
    CANCELLED_BY_CALLER = "CANCELLED_BY_CALLER"


@dataclass(frozen=True, order=True)
class FrameKey:
    epoch: int
    logical_channel: int
    sequence: int

    @classmethod
    def from_frame(cls, frame: Frame) -> "FrameKey":
        return cls(frame.epoch, frame.logical_channel, frame.sequence)


@dataclass(frozen=True, order=True)
class StaleHandle:
    session_id: str
    frame: FrameKey


@dataclass(frozen=True)
class AttemptToken:
    session_id: str
    frame: FrameKey
    attempt_no: int
    serializer: str
    resource: str
    resource_generation: int


@dataclass(frozen=True)
class TxAttempt:
    token: AttemptToken
    payload: bytes
    endpoints: tuple[str, ...]


@dataclass(frozen=True)
class EnqueueAck:
    status: EnqueueStatus
    frame: FrameKey | None


@dataclass(frozen=True)
class AdapterEvent:
    tick: int
    kind: str
    token: AttemptToken | None = None
    detail: str = ""


@dataclass(frozen=True)
class RestartReport:
    old_epoch: int
    new_epoch: int
    uncertain_attempts: int
    stale_queued_frames: int
    stale_queued: tuple[StaleHandle, ...]


@dataclass(frozen=True)
class TransportSnapshot:
    session_id: str
    epoch: int
    pending_frames: int
    commit_pending: tuple[AttemptToken, ...]
    inflight: tuple[AttemptToken, ...]
    uncertain_ranges: tuple[tuple[int, int, int, int], ...]
    unreconciled_stale: tuple[StaleHandle, ...]
    bank_states: tuple[tuple[str, str], ...]
    resource_generations: tuple[tuple[str, int], ...]


@dataclass(frozen=True)
class AdapterValidation:
    status: str
    anomalies: tuple[str, ...]
    queued_frames: int
    commit_pending: int
    inflight: int
    uncertain: int
    unreconciled_stale: int
    health_ready: bool


class Cf09TransportAdapter:
    """Deterministic token-aware scheduler and transport state machine."""

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
        session_id: str,
        queue_limit: int | None = None,
        epoch: int = 1,
    ) -> None:
        if not session_id.strip():
            raise ValueError("session_id must be a non-empty caller-supplied identity")
        if queue_limit is not None and queue_limit < 1:
            raise ValueError("queue_limit must be positive or None")
        if isinstance(epoch, bool) or not isinstance(epoch, int) or not 0 <= epoch <= U32_MAX:
            raise ValueError("epoch must fit the model CRC envelope unsigned 32-bit field")

        self.topology = topology
        self.session_id = session_id
        self.routes = build_routes(topology)
        self.channel_order = tuple(sorted(self.routes))
        self.queue_limit = queue_limit
        self.epoch = epoch
        self.queues: dict[int, Deque[Frame]] = {
            channel: deque() for channel in self.channel_order
        }
        self.next_sequence = {channel: 0 for channel in self.channel_order}
        self.bank_states = {
            Bank.TTL: BankState.SAFE_DISABLED,
            Bank.RS232: BankState.SAFE_DISABLED,
        }
        self.bridge_up: dict[str, bool | None] = {"A": None, "B": None}
        self.failed_endpoints: set[str] = set()

        self.required_resources = {route.resource for route in self.routes.values()}
        self.resource_generation = {resource: 0 for resource in self.required_resources}
        self.configured_generation: dict[str, int] = {}
        self.idle_generation: dict[str, int] = {}

        self.pending_by_serializer: dict[str, AttemptToken] = {}
        self.inflight_by_serializer: dict[str, AttemptToken] = {}
        self.frames_by_token: dict[AttemptToken, Frame] = {}
        self.attempt_states: dict[AttemptToken, AttemptState] = {}
        self._attempt_no_by_frame: dict[FrameKey, int] = defaultdict(int)
        self.uncertain_tokens: list[AttemptToken] = []
        self.committed_tokens: list[AttemptToken] = []
        self.completed_tokens: list[AttemptToken] = []
        self.observations: list[Observation] = []
        self.events: list[AdapterEvent] = []
        self._tick = 0
        self._round_robin_cursor = 0
        self.unreconciled_stale: dict[StaleHandle, Frame] = {}
        self.stale_resolution_ledger: dict[StaleHandle, str] = {}
        self._mutation_lock = Lock()
        self._active_mutation: str | None = None

    def _record(
        self,
        kind: str,
        *,
        token: AttemptToken | None = None,
        detail: str = "",
    ) -> AdapterEvent:
        self._tick += 1
        event = AdapterEvent(self._tick, kind, token, detail)
        self.events.append(event)
        return event

    def _route_for_resource(self, resource: str) -> Route:
        if resource not in self.required_resources:
            raise ValueError(f"resource outside topology: {resource}")
        return next(route for route in self.routes.values() if route.resource == resource)

    def _frame_for_token(self, token: AttemptToken) -> Frame:
        return self.frames_by_token[token]

    def _is_resource_ready(self, resource: str) -> bool:
        generation = self.resource_generation[resource]
        return (
            self.configured_generation.get(resource) == generation
            and self.idle_generation.get(resource) == generation
        )

    def _route_ready(self, route: Route) -> bool:
        if not self._is_resource_ready(route.resource):
            return False
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            return False
        if route.serializer in self.pending_by_serializer:
            return False
        if route.serializer in self.inflight_by_serializer:
            return False
        if any(
            self.bank_states[endpoint_bank(endpoint)] != BankState.ENABLED
            for endpoint in route.endpoints
        ):
            return False
        if any(endpoint in self.failed_endpoints for endpoint in route.endpoints):
            return False
        return True

    def _enqueue_internal(self, logical_channel: int, payload: bytes) -> EnqueueAck:
        if logical_channel not in self.queues:
            raise ValueError(f"logical channel outside topology: {logical_channel}")
        payload_bytes = bytes(payload)
        payload_sha256 = hashlib.sha256(payload_bytes).hexdigest().upper()
        queue = self.queues[logical_channel]
        sequence = self.next_sequence[logical_channel]
        if (
            isinstance(sequence, bool)
            or not isinstance(sequence, int)
            or not 0 <= sequence <= U32_MAX
        ):
            self._record(
                "SEQUENCE_SPACE_EXHAUSTED",
                detail=(
                    f"epoch={self.epoch};channel={logical_channel};sequence={sequence};"
                    f"payload_len={len(payload_bytes)};payload_sha256={payload_sha256};"
                    "enqueue_rejected=true"
                ),
            )
            raise OverflowError(
                "sequence no longer fits the model CRC envelope unsigned 32-bit field"
            )
        if self.queue_limit is not None and len(queue) >= self.queue_limit:
            self._record(
                "BACKPRESSURE",
                detail=(
                    f"epoch={self.epoch};channel={logical_channel};sequence={sequence};"
                    f"queue_limit={self.queue_limit};payload_len={len(payload_bytes)};"
                    f"payload_sha256={payload_sha256};silent_drop=false"
                ),
            )
            return EnqueueAck(EnqueueStatus.BACKPRESSURE, None)

        frame = Frame.create(self.epoch, logical_channel, sequence, payload_bytes)
        queue.append(frame)
        self.next_sequence[logical_channel] += 1
        key = FrameKey.from_frame(frame)
        self._record(
            "ENQUEUED",
            detail=(
                f"frame={key};payload_len={len(frame.payload)};"
                f"test_crc32=0x{frame.test_crc32:08X};"
                f"payload_sha256={payload_sha256}"
            ),
        )
        return EnqueueAck(EnqueueStatus.ENQUEUED, key)

    @_serialized_mutation
    def enqueue(self, logical_channel: int, payload: bytes) -> EnqueueAck:
        return self._enqueue_internal(logical_channel, payload)

    def pending_count(self) -> int:
        return sum(len(queue) for queue in self.queues.values())

    def _select_channel(self) -> int | None:
        count = len(self.channel_order)
        for offset in range(count):
            position = (self._round_robin_cursor + offset) % count
            channel = self.channel_order[position]
            if not self.queues[channel]:
                continue
            if not self._route_ready(self.routes[channel]):
                continue
            self._round_robin_cursor = (position + 1) % count
            return channel
        return None

    @_serialized_mutation
    def begin_next(self, logical_channel: int | None = None) -> TxAttempt | None:
        if logical_channel is None:
            logical_channel = self._select_channel()
            if logical_channel is None:
                return None
        elif logical_channel not in self.queues:
            raise ValueError(f"logical channel outside topology: {logical_channel}")

        queue = self.queues[logical_channel]
        if not queue:
            return None
        route = self.routes[logical_channel]
        if not self._route_ready(route):
            return None

        frame = queue[0]
        key = FrameKey.from_frame(frame)
        self._attempt_no_by_frame[key] += 1
        token = AttemptToken(
            session_id=self.session_id,
            frame=key,
            attempt_no=self._attempt_no_by_frame[key],
            serializer=route.serializer,
            resource=route.resource,
            resource_generation=self.resource_generation[route.resource],
        )
        self.pending_by_serializer[route.serializer] = token
        self.frames_by_token[token] = frame
        self.attempt_states[token] = AttemptState.COMMIT_PENDING
        self._record("ATTEMPT_BEGIN", token=token)
        return TxAttempt(token, frame.payload, route.endpoints)

    def _late_event(self, kind: str, token: AttemptToken, detail: str) -> bool:
        self._record(kind, token=token, detail=detail)
        return False

    def _callback_rejection_reason(
        self,
        token: AttemptToken,
        expected_state: AttemptState,
        current_slot: AttemptToken | None,
    ) -> str | None:
        if token.session_id != self.session_id:
            return "session_id_mismatch"
        if self.attempt_states.get(token) != expected_state:
            state = self.attempt_states.get(token)
            return f"attempt_state={state.value if state else 'UNKNOWN'}"
        if current_slot != token:
            return "token_not_current"
        route = self.routes.get(token.frame.logical_channel)
        if route is None:
            return "logical_channel_outside_topology"
        if route.serializer != token.serializer or route.resource != token.resource:
            return "route_binding_mismatch"
        if self.resource_generation.get(token.resource) != token.resource_generation:
            return "resource_generation_mismatch"
        return None

    @_serialized_mutation
    def resolve_commit(self, token: AttemptToken, outcome: CommitResult) -> bool:
        try:
            outcome = CommitResult(outcome)
        except ValueError as exc:
            self._record(
                "INVALID_COMMIT_OUTCOME",
                token=token,
                detail=f"outcome={outcome!r};state_unchanged",
            )
            raise ValueError(f"unsupported commit outcome: {outcome}") from exc
        rejection = self._callback_rejection_reason(
            token,
            AttemptState.COMMIT_PENDING,
            self.pending_by_serializer.get(token.serializer),
        )
        if rejection:
            return self._late_event(
                "LATE_COMMIT_EVENT",
                token,
                f"{rejection};state_unchanged",
            )

        frame = self._frame_for_token(token)
        queue = self.queues[frame.logical_channel]
        if not queue or queue[0] != frame:
            raise AssertionError("commit-pending frame is not at the queue head")

        rejection = self._callback_rejection_reason(
            token,
            AttemptState.COMMIT_PENDING,
            self.pending_by_serializer.get(token.serializer),
        )
        if rejection:
            return self._late_event(
                "LATE_COMMIT_EVENT",
                token,
                f"pre_mutation_{rejection};state_unchanged",
            )

        del self.pending_by_serializer[token.serializer]
        if outcome == CommitResult.NOT_COMMITTED:
            self.attempt_states[token] = AttemptState.NOT_COMMITTED
            self._record(
                "TRANSACTION_NOT_COMMITTED",
                token=token,
                detail="frame_retained;new_attempt_requires_explicit_begin_next",
            )
            return True

        queue.popleft()
        if outcome == CommitResult.UNKNOWN:
            self.attempt_states[token] = AttemptState.UNCERTAIN
            self.uncertain_tokens.append(token)
            self._record("UNCERTAIN", token=token, detail="commit_unknown;no_auto_retry")
            return True

        self.attempt_states[token] = AttemptState.INFLIGHT
        self.inflight_by_serializer[token.serializer] = token
        self.committed_tokens.append(token)
        self._record("ACCEPTED_BY_RESOURCE", token=token)
        return True

    def _mark_uncertain_internal(self, token: AttemptToken, reason: str) -> bool:
        state = self.attempt_states.get(token)
        if state == AttemptState.COMMIT_PENDING:
            current = self.pending_by_serializer.get(token.serializer)
            if current == token:
                del self.pending_by_serializer[token.serializer]
            frame = self._frame_for_token(token)
            queue = self.queues[frame.logical_channel]
            if queue and queue[0] == frame:
                queue.popleft()
        elif state == AttemptState.INFLIGHT:
            current = self.inflight_by_serializer.get(token.serializer)
            if current == token:
                del self.inflight_by_serializer[token.serializer]
        else:
            return self._late_event(
                "LATE_UNCERTAIN_EVENT",
                token,
                f"state={state.value if state else 'UNKNOWN'};state_unchanged",
            )

        self.attempt_states[token] = AttemptState.UNCERTAIN
        self.uncertain_tokens.append(token)
        self._record("UNCERTAIN", token=token, detail=f"{reason};no_auto_retry")
        return True

    @_serialized_mutation
    def mark_uncertain(self, token: AttemptToken, reason: str) -> bool:
        if not reason.strip():
            raise ValueError("uncertain reason must be non-empty")
        return self._mark_uncertain_internal(token, reason)

    @_serialized_mutation
    def resolve_complete(self, token: AttemptToken, *, evidence: str) -> bool:
        if not evidence.strip():
            raise ValueError("completion evidence must be a non-empty opaque label")
        rejection = self._callback_rejection_reason(
            token,
            AttemptState.INFLIGHT,
            self.inflight_by_serializer.get(token.serializer),
        )
        if rejection:
            return self._late_event(
                "LATE_COMPLETION_EVENT",
                token,
                f"{rejection};state_unchanged",
            )

        route = self.routes[token.frame.logical_channel]
        unsafe_reasons: list[str] = []
        if not self._is_resource_ready(route.resource):
            unsafe_reasons.append("resource_not_ready")
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            unsafe_reasons.append(f"bridge_{route.bridge}_down")
        if any(
            self.bank_states[endpoint_bank(endpoint)] != BankState.ENABLED
            for endpoint in route.endpoints
        ):
            unsafe_reasons.append("bank_not_enabled")
        failed = sorted(endpoint for endpoint in route.endpoints if endpoint in self.failed_endpoints)
        if failed:
            unsafe_reasons.append(f"endpoint_fault={','.join(failed)}")
        if unsafe_reasons:
            self._mark_uncertain_internal(token, ";".join(unsafe_reasons))
            return False

        rejection = self._callback_rejection_reason(
            token,
            AttemptState.INFLIGHT,
            self.inflight_by_serializer.get(token.serializer),
        )
        if rejection:
            return self._late_event(
                "LATE_COMPLETION_EVENT",
                token,
                f"pre_mutation_{rejection};state_unchanged",
            )

        del self.inflight_by_serializer[token.serializer]
        self.attempt_states[token] = AttemptState.COMPLETE
        self.completed_tokens.append(token)
        frame = self._frame_for_token(token)
        self.observations.extend(
            Observation.from_frame(endpoint, route.serializer, frame)
            for endpoint in route.endpoints
        )
        self._record(
            "MODEL_SERIALIZER_COMPLETE",
            token=token,
            detail=f"evidence={evidence};not_connector_measurement",
        )
        return True

    def _transition_bank(
        self,
        bank: Bank,
        target: BankState,
        *,
        reason: str = "",
    ) -> None:
        current = self.bank_states[bank]
        if target not in self._ALLOWED_BANK_TRANSITIONS[current]:
            raise ValueError(f"illegal bank transition: {bank.value} {current.value} -> {target.value}")
        self.bank_states[bank] = target
        detail = f"{bank.value}:{current.value}->{target.value}"
        if reason:
            detail += f";reason={reason}"
        self._record("BANK_STATE", detail=detail)
        if target == BankState.SAFE_DISABLED:
            affected = [
                token
                for token in tuple(self.pending_by_serializer.values())
                + tuple(self.inflight_by_serializer.values())
                if any(
                    endpoint_bank(endpoint) == bank
                    for endpoint in self.routes[token.frame.logical_channel].endpoints
                )
            ]
            for token in affected:
                self._mark_uncertain_internal(token, f"bank_{bank.value}_disabled_after_begin")

    @_serialized_mutation
    def disable_bank(self, bank: Bank, *, reason: str) -> None:
        """Expose only the safe-direction transition; enabling stays preflighted."""

        if not reason.strip():
            raise ValueError("bank disable reason must be non-empty")
        if self.bank_states[bank] == BankState.SAFE_DISABLED:
            self._record(
                "BANK_STATE",
                detail=f"{bank.value}:SAFE_DISABLED->SAFE_DISABLED;reason={reason}",
            )
            return
        self._transition_bank(bank, BankState.SAFE_DISABLED, reason=reason)

    def _route_banks(self, route: Route) -> set[Bank]:
        return {endpoint_bank(endpoint) for endpoint in route.endpoints}

    def _mark_resource_configured_internal(self, resource: str, generation: int) -> None:
        route = self._route_for_resource(resource)
        current = self.resource_generation[resource]
        if generation != current:
            raise ValueError(f"stale resource generation: supplied={generation};current={current}")
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            raise RuntimeError(f"cannot configure {resource} while bridge {route.bridge} is down")
        outstanding = [
            token
            for token in tuple(self.pending_by_serializer.values())
            + tuple(self.inflight_by_serializer.values())
            if token.resource == resource
        ]
        if outstanding:
            self._record(
                "RESOURCE_RECONFIG_REJECTED",
                token=outstanding[0],
                detail="invalidate_resource_required_before_reconfigure;state_unchanged",
            )
            raise RuntimeError(
                f"resource has an open attempt; invalidate it before reconfigure: {resource}"
            )
        unsafe_banks = sorted(
            bank.value
            for bank in self._route_banks(route)
            if self.bank_states[bank] != BankState.SAFE_DISABLED
        )
        if unsafe_banks:
            raise RuntimeError(
                f"safe-disable affected banks before configuring {resource}: {unsafe_banks}"
            )
        self.configured_generation[resource] = generation
        self.idle_generation.pop(resource, None)
        self._record("RESOURCE_CONFIGURED", detail=f"resource={resource};generation={generation}")

    @_serialized_mutation
    def mark_resource_configured(self, resource: str, generation: int) -> None:
        self._mark_resource_configured_internal(resource, generation)

    def _mark_resource_idle_internal(self, resource: str, generation: int) -> None:
        route = self._route_for_resource(resource)
        current = self.resource_generation[resource]
        if generation != current:
            raise ValueError(f"stale resource generation: supplied={generation};current={current}")
        if self.configured_generation.get(resource) != generation:
            raise RuntimeError(f"resource must be configured before idle confirmation: {resource}")
        if route.bridge is not None and not self.bridge_up[route.bridge]:
            raise RuntimeError(f"cannot confirm {resource} idle while bridge {route.bridge} is down")
        unsafe_banks = sorted(
            bank.value
            for bank in self._route_banks(route)
            if self.bank_states[bank] != BankState.SAFE_DISABLED
        )
        if unsafe_banks:
            raise RuntimeError(
                f"safe-disable affected banks before confirming {resource} idle: {unsafe_banks}"
            )
        self.idle_generation[resource] = generation
        self._record("RESOURCE_IDLE_CONFIRMED", detail=f"resource={resource};generation={generation}")

    @_serialized_mutation
    def mark_resource_idle(self, resource: str, generation: int) -> None:
        self._mark_resource_idle_internal(resource, generation)

    @_serialized_mutation
    def declare_all_resources_ready(self) -> None:
        """MODEL_ONLY convenience; each resource still gets explicit config/idle events."""

        for resource in sorted(self.required_resources):
            generation = self.resource_generation[resource]
            self._mark_resource_configured_internal(resource, generation)
            self._mark_resource_idle_internal(resource, generation)

    @_serialized_mutation
    def arm_outputs(self) -> None:
        missing = sorted(
            resource for resource in self.required_resources if not self._is_resource_ready(resource)
        )
        down_bridges = sorted(
            {
                route.bridge
                for route in self.routes.values()
                if route.bridge is not None and not self.bridge_up[route.bridge]
            }
        )
        non_safe_banks = {
            bank.value: state.value
            for bank, state in self.bank_states.items()
            if state != BankState.SAFE_DISABLED
        }
        if missing or down_bridges or self.failed_endpoints:
            raise RuntimeError(
                "output arm preconditions not met: "
                f"missing_resources={missing};down_bridges={down_bridges};"
                f"failed_endpoints={sorted(self.failed_endpoints)}"
            )
        if non_safe_banks:
            raise ValueError(f"all banks must start SAFE_DISABLED: {non_safe_banks}")

        for bank in (Bank.TTL, Bank.RS232):
            self._transition_bank(bank, BankState.CONFIGURING)
            self._transition_bank(bank, BankState.IDLE_CONFIRMED)
            self._transition_bank(bank, BankState.ENABLED)

    def _force_safe_disabled_internal(self, reason: str) -> None:
        for bank in (Bank.TTL, Bank.RS232):
            previous = self.bank_states[bank]
            self.bank_states[bank] = BankState.SAFE_DISABLED
            self._record(
                "BANK_STATE",
                detail=f"{bank.value}:{previous.value}->SAFE_DISABLED;reason={reason}",
            )
        outstanding = tuple(self.pending_by_serializer.values()) + tuple(
            self.inflight_by_serializer.values()
        )
        for token in outstanding:
            self._mark_uncertain_internal(token, f"safe_disable;reason={reason}")

    @_serialized_mutation
    def force_safe_disabled(self, reason: str) -> None:
        if not reason.strip():
            raise ValueError("safe-disable reason must be non-empty")
        self._force_safe_disabled_internal(reason)

    def _invalidate_resources(self, resources: Iterable[str], reason: str) -> None:
        selected = set(resources)
        for resource in sorted(selected):
            self._route_for_resource(resource)
            self.resource_generation[resource] += 1
            self.configured_generation.pop(resource, None)
            self.idle_generation.pop(resource, None)
            self._record(
                "RESOURCE_GENERATION_INVALIDATED",
                detail=(
                    f"resource={resource};generation={self.resource_generation[resource]};"
                    f"reason={reason}"
                ),
            )

        affected = [
            token
            for token in tuple(self.pending_by_serializer.values())
            + tuple(self.inflight_by_serializer.values())
            if token.resource in selected
        ]
        for token in affected:
            self._mark_uncertain_internal(token, f"resource_invalidated;reason={reason}")

    @_serialized_mutation
    def invalidate_resource(self, resource: str, *, reason: str) -> None:
        """Invalidate one local or bridge resource and quarantine its open attempt."""

        if not reason.strip():
            raise ValueError("resource invalidation reason must be non-empty")
        self._invalidate_resources((resource,), reason)

    @_serialized_mutation
    def set_bridge_state(self, bridge: str, *, up: bool, reason: str) -> None:
        if bridge not in self.bridge_up:
            raise ValueError("bridge must be A or B")
        if not reason.strip():
            raise ValueError("bridge-state reason must be non-empty")
        self.bridge_up[bridge] = up
        detail = f"bridge={bridge};reason={reason}"
        if not up:
            detail += ";physical_output_safety=UNKNOWN_HARDWARE_REQUIRED"
        self._record("BRIDGE_UP" if up else "BRIDGE_DOWN", detail=detail)
        if not up:
            resources = {
                route.resource for route in self.routes.values() if route.bridge == bridge
            }
            self._invalidate_resources(resources, f"bridge_{bridge}_down")

    @_serialized_mutation
    def inject_endpoint_fault(self, endpoint: str) -> None:
        known = {endpoint for route in self.routes.values() for endpoint in route.endpoints}
        if endpoint not in known:
            raise ValueError(f"endpoint outside topology: {endpoint}")
        self.failed_endpoints.add(endpoint)
        self._record("ENDPOINT_FAULT_INJECTED", detail=f"endpoint={endpoint}")
        affected = [
            token
            for token in tuple(self.pending_by_serializer.values())
            + tuple(self.inflight_by_serializer.values())
            if endpoint in self.routes[token.frame.logical_channel].endpoints
        ]
        for token in affected:
            self._mark_uncertain_internal(token, f"endpoint_fault={endpoint}")

    @_serialized_mutation
    def clear_endpoint_fault(self, endpoint: str) -> None:
        known = {endpoint for route in self.routes.values() for endpoint in route.endpoints}
        if endpoint not in known:
            raise ValueError(f"endpoint outside topology: {endpoint}")
        if endpoint not in self.failed_endpoints:
            self._record("ENDPOINT_FAULT_CLEAR_NOOP", detail=f"endpoint={endpoint}")
            return
        self.failed_endpoints.discard(endpoint)
        self._record("ENDPOINT_FAULT_CLEARED", detail=f"endpoint={endpoint}")

    @_serialized_mutation
    def restart(self, new_epoch: int, *, reason: str) -> RestartReport:
        if isinstance(new_epoch, bool) or not isinstance(new_epoch, int):
            raise ValueError("new_epoch must be an unsigned 32-bit integer")
        if new_epoch <= self.epoch:
            raise ValueError("new_epoch must be greater than the current epoch")
        if new_epoch > U32_MAX:
            raise ValueError("new_epoch must fit the model CRC envelope unsigned 32-bit field")
        if not reason.strip():
            raise ValueError("restart reason must be non-empty")

        uncertain_before = len(self.uncertain_tokens)
        self._force_safe_disabled_internal(reason)
        stale_count = 0
        stale_handles: list[StaleHandle] = []
        for channel, queue in self.queues.items():
            while queue:
                frame = queue.popleft()
                stale_count += 1
                key = FrameKey.from_frame(frame)
                handle = StaleHandle(self.session_id, key)
                stale_handles.append(handle)
                self.unreconciled_stale[handle] = frame
                self._record(
                    "STALE_QUEUE_RECONCILIATION_REQUIRED",
                    detail=(
                        f"handle={handle};classification=NOT_COMMITTED_BUT_DROPPED_FROM_QUEUE;"
                        "silent_replay=false"
                    ),
                )

        self._invalidate_resources(self.required_resources, f"restart;reason={reason}")
        old_epoch = self.epoch
        self.epoch = new_epoch
        self.next_sequence = {channel: 0 for channel in self.channel_order}
        self._round_robin_cursor = 0
        self._record("RESTART", detail=f"epoch={old_epoch}->{new_epoch};reason={reason}")
        return RestartReport(
            old_epoch=old_epoch,
            new_epoch=new_epoch,
            uncertain_attempts=len(self.uncertain_tokens) - uncertain_before,
            stale_queued_frames=stale_count,
            stale_queued=tuple(sorted(stale_handles)),
        )

    @_serialized_mutation
    def reconcile_stale(
        self,
        old: StaleHandle,
        *,
        resolution: StaleResolution,
        reason: str,
    ) -> EnqueueAck | None:
        """Resolve a restart-discarded enqueue only by explicit caller policy."""

        if not reason.strip():
            raise ValueError("stale-frame reconciliation reason must be non-empty")
        if old.session_id != self.session_id:
            self._record(
                "FOREIGN_STALE_RECONCILIATION",
                detail=(
                    f"handle={old};adapter_session={self.session_id};state_unchanged"
                ),
            )
            raise ValueError("stale handle belongs to another session")
        try:
            resolution = StaleResolution(resolution)
        except ValueError as exc:
            self._record(
                "INVALID_STALE_RESOLUTION",
                detail=f"handle={old};resolution={resolution!r};state_unchanged",
            )
            raise ValueError(f"unsupported stale resolution: {resolution}") from exc

        frame = self.unreconciled_stale.get(old)
        if frame is None:
            self._record(
                "LATE_STALE_RECONCILIATION",
                detail=f"handle={old};resolution={resolution.value};state_unchanged",
            )
            raise KeyError(f"stale handle is unknown or already reconciled: {old}")

        if resolution == StaleResolution.CANCELLED_BY_CALLER:
            del self.unreconciled_stale[old]
            self.stale_resolution_ledger[old] = f"{resolution.value};reason={reason}"
            self._record(
                "STALE_QUEUE_CANCELLED",
                detail=f"handle={old};reason={reason}",
            )
            return None

        ack = self._enqueue_internal(frame.logical_channel, frame.payload)
        if ack.status == EnqueueStatus.BACKPRESSURE:
            self._record(
                "STALE_QUEUE_RECONCILIATION_DEFERRED",
                detail=f"handle={old};reason={reason};backpressure=true",
            )
            return ack

        del self.unreconciled_stale[old]
        self.stale_resolution_ledger[old] = (
            f"{resolution.value};replacement={ack.frame};reason={reason}"
        )
        self._record(
            "STALE_QUEUE_REENQUEUED",
            detail=f"old={old};replacement={ack.frame};reason={reason}",
        )
        return ack

    def uncertain_ranges(self) -> list[dict[str, int]]:
        grouped: dict[tuple[int, int], set[int]] = defaultdict(set)
        for token in self.uncertain_tokens:
            grouped[(token.frame.epoch, token.frame.logical_channel)].add(token.frame.sequence)

        result: list[dict[str, int]] = []
        for (epoch, channel), values in sorted(grouped.items()):
            ordered = sorted(values)
            start = previous = ordered[0]
            for value in ordered[1:]:
                if value == previous + 1:
                    previous = value
                    continue
                result.append(
                    {"epoch": epoch, "logical_channel": channel, "start": start, "end": previous}
                )
                start = previous = value
            result.append(
                {"epoch": epoch, "logical_channel": channel, "start": start, "end": previous}
            )
        return result

    def expected_observations(self) -> list[Observation]:
        expected: list[Observation] = []
        for token in self.committed_tokens:
            route = self.routes[token.frame.logical_channel]
            frame = self._frame_for_token(token)
            expected.extend(
                Observation.from_frame(endpoint, route.serializer, frame)
                for endpoint in route.endpoints
            )
        return expected

    def observation_anomalies(self) -> list[str]:
        """Return capture/manifest anomalies only; use validate() for a PASS decision."""

        return ReceiverOracle(self.topology).validate(
            self.observations,
            self.expected_observations(),
        )

    def health_anomalies(self) -> list[str]:
        """Return operational readiness/fault reasons for the selected topology."""

        anomalies: list[str] = []
        required_bridges = sorted(
            {
                route.bridge
                for route in self.routes.values()
                if route.bridge is not None
            }
        )
        for bridge in required_bridges:
            state = self.bridge_up[bridge]
            if state is None:
                anomalies.append(f"BRIDGE_STATE_UNKNOWN:{bridge}")
            elif not state:
                anomalies.append(f"BRIDGE_DOWN:{bridge}")
        for resource in sorted(self.required_resources):
            if not self._is_resource_ready(resource):
                anomalies.append(
                    f"RESOURCE_NOT_READY:{resource}:"
                    f"generation={self.resource_generation[resource]}"
                )
        if self.epoch == U32_MAX:
            anomalies.append(f"EPOCH_SPACE_EXHAUSTED:epoch={self.epoch}")
        for channel, sequence in sorted(self.next_sequence.items()):
            if (
                isinstance(sequence, bool)
                or not isinstance(sequence, int)
                or not 0 <= sequence <= U32_MAX
            ):
                anomalies.append(
                    f"SEQUENCE_SPACE_EXHAUSTED:channel={channel}:next_sequence={sequence}"
                )
        for bank, state in sorted(self.bank_states.items(), key=lambda item: item[0].value):
            if state != BankState.ENABLED:
                anomalies.append(f"BANK_NOT_ENABLED:{bank.value}:{state.value}")
        for endpoint in sorted(self.failed_endpoints):
            anomalies.append(f"ENDPOINT_FAULT_ACTIVE:{endpoint}")
        return anomalies

    def validate(self) -> AdapterValidation:
        """Return a conservative lifecycle-plus-observation validation result."""

        lifecycle: list[str] = []
        for channel, queue in sorted(self.queues.items()):
            for frame in queue:
                lifecycle.append(
                    f"QUEUED_NOT_TERMINAL:epoch={frame.epoch}:channel={channel}:"
                    f"sequence={frame.sequence}"
                )
        for token in sorted(self.pending_by_serializer.values(), key=lambda item: item.frame):
            lifecycle.append(f"COMMIT_PENDING:{token}")
        for token in sorted(self.inflight_by_serializer.values(), key=lambda item: item.frame):
            lifecycle.append(f"MISSING_COMPLETION:{token}")
        for token in sorted(self.uncertain_tokens, key=lambda item: item.frame):
            lifecycle.append(f"UNCERTAIN:{token}")
        for key in sorted(self.unreconciled_stale):
            lifecycle.append(f"UNRECONCILED_STALE_QUEUE:{key}")

        observation = self.observation_anomalies()
        health = self.health_anomalies()
        explicit_fault = any(
            item.startswith("BRIDGE_DOWN:") or item.startswith("ENDPOINT_FAULT_ACTIVE:")
            for item in health
        )
        identity_space_exhausted = any(
            item.startswith("SEQUENCE_SPACE_EXHAUSTED:")
            or item.startswith("EPOCH_SPACE_EXHAUSTED:")
            for item in health
        )
        if self.uncertain_tokens:
            status = "TX_ADAPTER_MODEL_UNCERTAIN"
        elif self.unreconciled_stale:
            status = "TX_ADAPTER_MODEL_RECONCILIATION_REQUIRED"
        elif identity_space_exhausted:
            status = "TX_ADAPTER_MODEL_EXHAUSTED"
        elif explicit_fault:
            status = "TX_ADAPTER_MODEL_FAULTED"
        elif self.pending_count() or self.pending_by_serializer or self.inflight_by_serializer:
            status = "TX_ADAPTER_MODEL_INCOMPLETE"
        elif observation:
            status = "TX_ADAPTER_MODEL_FAIL"
        elif health:
            status = "TX_ADAPTER_MODEL_NOT_READY"
        else:
            status = "TX_ADAPTER_MODEL_PASS"
        return AdapterValidation(
            status=status,
            anomalies=tuple(lifecycle + observation + health),
            queued_frames=self.pending_count(),
            commit_pending=len(self.pending_by_serializer),
            inflight=len(self.inflight_by_serializer),
            uncertain=len(self.uncertain_tokens),
            unreconciled_stale=len(self.unreconciled_stale),
            health_ready=not health,
        )

    def snapshot(self) -> TransportSnapshot:
        ranges = tuple(
            (item["epoch"], item["logical_channel"], item["start"], item["end"])
            for item in self.uncertain_ranges()
        )
        return TransportSnapshot(
            session_id=self.session_id,
            epoch=self.epoch,
            pending_frames=self.pending_count(),
            commit_pending=tuple(
                sorted(self.pending_by_serializer.values(), key=lambda token: token.frame)
            ),
            inflight=tuple(
                sorted(self.inflight_by_serializer.values(), key=lambda token: token.frame)
            ),
            uncertain_ranges=ranges,
            unreconciled_stale=tuple(sorted(self.unreconciled_stale)),
            bank_states=tuple(
                sorted((bank.value, state.value) for bank, state in self.bank_states.items())
            ),
            resource_generations=tuple(sorted(self.resource_generation.items())),
        )

    def event_digest(self) -> str:
        canonical = json.dumps(
            [asdict(event) for event in self.events],
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
        return hashlib.sha256(canonical).hexdigest().upper()
