"""Exact-part-neutral fake SPI/loopback model for CF-09 source class D.

This module is an ASSUMPTION / MODEL_ONLY integration prototype.  It keeps the
existing ``Cf09TransportAdapter`` as the logical token/queue state machine and
adds an explicit projection to the source-class-D physical allocation:

* L0 -> local UART1
* L1 -> local UART2
* L2 -> physical bridge A channel 0
* L3 -> physical bridge A channel 1
* L4 -> physical bridge B channel 0
* physical bridge B channel 1 -> dedicated RX reservation, never a TX target

The existing mirrored transport model labels L4 as logical ``BRIDGE_A_CH2``.
That label is deliberately not mutated here.  ``DClassProjection`` validates a
separate logical-to-physical projection against the versioned scenario JSON so
tests cannot silently mistake the one-quad route table for the two-dual design.

The fake bus separates three events that a real driver must not conflate:

1. host SPI transaction pending;
2. bytes accepted by the model serializer (host commit);
3. model serializer completion reported by IRQ or an explicit status poll.

No register map, FIFO depth, SPI rate, IRQ polarity/latency, reset waveform,
UART baud/framing, power-off behaviour, electrical isolation or connector
delivery is claimed.  Exact-part and hardware statuses therefore remain
UNKNOWN / NOT_RUN even when every model test passes.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import asdict, dataclass
from enum import Enum
from functools import wraps
from pathlib import Path
from threading import Lock

from cf09_host_model_v1 import CommitResult, Topology
from cf09_source_allocation_model_v1 import (
    AllocationStatus,
    allocate_mirrored_5,
    load_scenario_set,
)
from cf09_transport_adapter_v1 import (
    AttemptState,
    AttemptToken,
    Cf09TransportAdapter,
    TxAttempt,
)


MODEL_CLASS = "ASSUMPTION / MODEL_ONLY"
MODEL_SCOPE = "D_CLASS_TWO_DUAL_SHARED_SPI_FAKE_LOOPBACK"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"
EXACT_PART_STATUS = "UNKNOWN / NOT_SELECTED"
CONNECTOR_DELIVERY_STATUS = "NOT_RUN / NOT_PROVEN"
RESET_POLICY = "GLOBAL_SAFE_DISABLE_BEFORE_RECONFIGURATION"
RX_RESERVATION = "BRIDGE_B_CH1"

DEFAULT_SCENARIO_PATH = (
    Path(__file__).resolve().parents[1]
    / "engineering"
    / "cf09-serial-source-scenarios-v1.0.json"
)


def _serialized_mutation(method):
    """Reject reentrant/concurrent rig mutations without partial state."""

    @wraps(method)
    def guarded(self, *args, **kwargs):
        if not self._mutation_lock.acquire(blocking=False):
            active = self._active_mutation or "another_thread"
            raise RuntimeError(
                f"concurrent/reentrant rig mutation rejected: active={active};"
                f"requested={method.__name__}"
            )
        self._active_mutation = method.__name__
        try:
            return method(self, *args, **kwargs)
        finally:
            self._active_mutation = None
            self._mutation_lock.release()

    return guarded


class StartStatus(str, Enum):
    STARTED = "STARTED"
    BUS_BUSY = "BUS_BUSY"


class TransferState(str, Enum):
    HOST_PENDING = "HOST_PENDING"
    NOT_COMMITTED = "NOT_COMMITTED"
    HOST_COMMITTED = "HOST_COMMITTED"
    SERIALIZER_DONE_LATCHED = "SERIALIZER_DONE_LATCHED"
    COMPLETE = "COMPLETE"
    UNCERTAIN = "UNCERTAIN"


class IrqFault(str, Enum):
    NORMAL = "NORMAL"
    SUPPRESSED = "SUPPRESSED"
    STUCK_ASSERTED = "STUCK_ASSERTED"


@dataclass(frozen=True)
class PhysicalUart:
    device_id: str
    channel: int


@dataclass(frozen=True)
class ProjectionBinding:
    logical_channel: int
    logical_resource: str
    scenario_resource: str
    physical: PhysicalUart | None
    endpoints: tuple[str, str]


@dataclass(frozen=True)
class SpiTransferToken:
    transfer_no: int
    parent: AttemptToken
    bus_generation: int
    device_generation: int
    device_id: str
    physical_channel: int
    logical_resource: str
    scenario_resource: str
    chip_select_id: str
    payload_len: int
    payload_sha256: str


@dataclass(frozen=True)
class StartAck:
    status: StartStatus
    transfer: SpiTransferToken | None


@dataclass(frozen=True)
class RigEvent:
    tick: int
    kind: str
    transfer: SpiTransferToken | None = None
    parent: AttemptToken | None = None
    detail: str = ""


@dataclass(frozen=True)
class RigValidation:
    status: str
    anomalies: tuple[str, ...]
    accepted_by_serializer: int
    completed_by_serializer: int
    hardware_status: str = HARDWARE_STATUS
    exact_part_status: str = EXACT_PART_STATUS
    connector_delivery_status: str = CONNECTOR_DELIVERY_STATUS


class DClassProjection:
    """Validated projection from the logical mirrored model to class-D hardware."""

    _PHYSICAL_BY_SCENARIO_RESOURCE = {
        "BRIDGE_A_CH0": PhysicalUart("A", 0),
        "BRIDGE_A_CH1": PhysicalUart("A", 1),
        "BRIDGE_B_CH0": PhysicalUart("B", 0),
    }

    def __init__(
        self,
        adapter: Cf09TransportAdapter,
        *,
        scenario_path: Path = DEFAULT_SCENARIO_PATH,
    ) -> None:
        if adapter.topology != Topology.mirrored():
            raise ValueError("class-D projection requires the five-channel mirrored topology")

        expected_logical_resources = {
            0: "UART1",
            1: "UART2",
            2: "BRIDGE_A_CH0",
            3: "BRIDGE_A_CH1",
            4: "BRIDGE_A_CH2",
        }
        actual_logical_resources = {
            channel: route.resource for channel, route in adapter.routes.items()
        }
        if actual_logical_resources != expected_logical_resources:
            raise ValueError(
                "unsupported logical transport route table for class-D projection: "
                f"{actual_logical_resources}"
            )

        scenario_set = load_scenario_set(Path(scenario_path))
        scenarios = {
            item["scenario_id"]: item for item in scenario_set["scenarios"]
        }
        scenario = scenarios.get("D_2_UART_PLUS_TWO_DUAL")
        if scenario is None:
            raise ValueError("D_2_UART_PLUS_TWO_DUAL scenario is missing")
        allocation = allocate_mirrored_5(scenario)
        if allocation.status != AllocationStatus.PROVISIONAL:
            raise ValueError(
                "class-D source allocation is not provisionally available: "
                f"{allocation.status.value}"
            )
        if allocation.rx.resource is None:
            raise ValueError("class-D dedicated RX reservation is missing")
        if allocation.rx.resource.resource_id != RX_RESERVATION:
            raise ValueError(
                "class-D RX reservation changed: "
                f"expected={RX_RESERVATION};actual={allocation.rx.resource.resource_id}"
            )

        logical_resource_by_channel = expected_logical_resources
        bindings: dict[int, ProjectionBinding] = {}
        for tx in allocation.tx:
            physical = self._PHYSICAL_BY_SCENARIO_RESOURCE.get(
                tx.resource.resource_id
            )
            route = adapter.routes[tx.logical_channel]
            if tuple(tx.endpoints) != route.endpoints:
                raise ValueError(
                    "class-D scenario endpoints do not match logical transport: "
                    f"channel={tx.logical_channel};scenario={tx.endpoints};"
                    f"logical={route.endpoints}"
                )
            bindings[tx.logical_channel] = ProjectionBinding(
                logical_channel=tx.logical_channel,
                logical_resource=logical_resource_by_channel[tx.logical_channel],
                scenario_resource=tx.resource.resource_id,
                physical=physical,
                endpoints=tuple(tx.endpoints),
            )

        if set(bindings) != set(range(5)):
            raise ValueError(f"class-D projection is incomplete: {sorted(bindings)}")
        expected_scenario_resources = (
            "UART1",
            "UART2",
            "BRIDGE_A_CH0",
            "BRIDGE_A_CH1",
            "BRIDGE_B_CH0",
        )
        if tuple(bindings[index].scenario_resource for index in range(5)) != (
            expected_scenario_resources
        ):
            raise ValueError("class-D scenario allocation changed unexpectedly")

        self.bindings = bindings
        self.rx_reservation = allocation.rx.resource.resource_id
        self.scenario_path = Path(scenario_path)

    def external_for_logical(self, logical_channel: int) -> ProjectionBinding:
        if logical_channel not in self.bindings:
            raise ValueError(f"logical channel outside D projection: {logical_channel}")
        binding = self.bindings[logical_channel]
        if binding.physical is None:
            raise ValueError(
                f"logical channel {logical_channel} is local and has no SPI target"
            )
        return binding

    def logical_for_physical_tx(self, device_id: str, channel: int) -> int:
        if device_id not in {"A", "B"}:
            raise ValueError(f"unknown physical bridge device: {device_id}")
        if channel not in {0, 1}:
            raise ValueError(f"physical bridge channel outside 0..1: {channel}")
        if (device_id, channel) == ("B", 1):
            raise ValueError("BRIDGE_B_CH1 is reserved for RX and cannot be a TX target")
        for logical_channel, binding in self.bindings.items():
            if binding.physical == PhysicalUart(device_id, channel):
                return logical_channel
        raise ValueError(
            f"physical bridge channel is not allocated for class-D TX: {device_id}{channel}"
        )

    def logical_resources_for_device(self, device_id: str) -> tuple[str, ...]:
        if device_id not in {"A", "B"}:
            raise ValueError(f"unknown physical bridge device: {device_id}")
        return tuple(
            binding.logical_resource
            for binding in self.bindings.values()
            if binding.physical is not None
            and binding.physical.device_id == device_id
        )


class FakeSharedSpiBus:
    """Symbolic shared-bus ownership and chip-select arbiter."""

    def __init__(self) -> None:
        self.owner_id: str | None = None
        self.initialization_count = 0
        self.devices: dict[str, str] = {}
        self.active: SpiTransferToken | None = None
        self.generation = 0
        self.online = True

    def initialize(self, owner_id: str) -> None:
        if not owner_id.strip():
            raise ValueError("bus owner_id must be non-empty")
        if self.owner_id is not None:
            raise RuntimeError(
                f"shared SPI bus is already initialized by {self.owner_id}"
            )
        self.owner_id = owner_id
        self.initialization_count = 1

    def attach_device(self, owner_id: str, device_id: str, chip_select_id: str) -> None:
        if owner_id != self.owner_id:
            raise ValueError("only the initialized bus owner may attach devices")
        if device_id in self.devices:
            raise ValueError(f"duplicate SPI device id: {device_id}")
        if not chip_select_id.strip():
            raise ValueError("chip-select identity must be non-empty")
        if chip_select_id in self.devices.values():
            raise ValueError(f"duplicate chip-select identity: {chip_select_id}")
        self.devices[device_id] = chip_select_id

    def try_acquire(self, transfer: SpiTransferToken) -> bool:
        if not self.online:
            raise RuntimeError("shared SPI bus is offline")
        expected_cs = self.devices.get(transfer.device_id)
        if expected_cs is None:
            raise ValueError(f"unattached SPI device: {transfer.device_id}")
        if expected_cs != transfer.chip_select_id:
            raise ValueError(
                "chip-select does not match attached device: "
                f"device={transfer.device_id};expected={expected_cs};"
                f"actual={transfer.chip_select_id}"
            )
        if transfer.bus_generation != self.generation:
            raise ValueError("transfer carries a stale bus generation")
        if self.active is not None:
            return False
        self.active = transfer
        return True

    def release(self, transfer: SpiTransferToken) -> bool:
        if self.active != transfer:
            return False
        self.active = None
        return True

    def reset(self) -> SpiTransferToken | None:
        interrupted = self.active
        self.active = None
        self.generation += 1
        self.online = False
        return interrupted

    def recover(self) -> None:
        if self.online:
            raise RuntimeError("shared SPI bus is already online")
        self.online = True


class DClassDualBridgeHarness:
    """Token-safe D-class fake SPI driver and IRQ/reset fault harness."""

    OWNER_ID = "CF09_D_CLASS_MODEL_OWNER"

    def __init__(
        self,
        adapter: Cf09TransportAdapter,
        *,
        scenario_path: Path = DEFAULT_SCENARIO_PATH,
        chip_selects: dict[str, str] | None = None,
    ) -> None:
        self.adapter = adapter
        self.projection = DClassProjection(
            adapter,
            scenario_path=scenario_path,
        )
        supplied = dict(chip_selects or {"A": "MODEL_CS_A", "B": "MODEL_CS_B"})
        if set(supplied) != {"A", "B"}:
            raise ValueError("chip_selects must define exactly physical devices A and B")

        self.bus = FakeSharedSpiBus()
        self.bus.initialize(self.OWNER_ID)
        for device_id in ("A", "B"):
            self.bus.attach_device(
                self.OWNER_ID,
                device_id,
                supplied[device_id],
            )

        self.device_generation = {"A": 0, "B": 0}
        self.device_in_reset = {"A": False, "B": False}
        self.irq_fault = {"A": IrqFault.NORMAL, "B": IrqFault.NORMAL}
        self.irq_asserted = {"A": False, "B": False}
        self.completion_latches: dict[str, list[SpiTransferToken]] = {
            "A": [],
            "B": [],
        }
        self.transfer_states: dict[SpiTransferToken, TransferState] = {}
        self.accepted_transfers: list[SpiTransferToken] = []
        self.completed_transfers: list[SpiTransferToken] = []
        self.events: list[RigEvent] = []
        self._next_transfer_no = 1
        self._tick = 0
        self._mutation_lock = Lock()
        self._active_mutation: str | None = None

    def _record(
        self,
        kind: str,
        *,
        transfer: SpiTransferToken | None = None,
        parent: AttemptToken | None = None,
        detail: str = "",
    ) -> RigEvent:
        self._tick += 1
        event = RigEvent(
            self._tick,
            kind,
            transfer,
            parent,
            detail,
        )
        self.events.append(event)
        return event

    @staticmethod
    def _validate_device(device_id: str) -> None:
        if device_id not in {"A", "B"}:
            raise ValueError(f"physical bridge device must be A or B: {device_id}")

    def _parent_is_current_commit_pending(self, attempt: TxAttempt) -> bool:
        token = attempt.token
        return (
            self.adapter.attempt_states.get(token) == AttemptState.COMMIT_PENDING
            and self.adapter.pending_by_serializer.get(token.serializer) == token
        )

    def _validate_attempt_binding(
        self,
        attempt: TxAttempt,
        binding: ProjectionBinding,
    ) -> None:
        """Bind caller-supplied payload/endpoints to the immutable parent frame."""

        token = attempt.token
        route = self.adapter.routes.get(token.frame.logical_channel)
        frame = self.adapter.frames_by_token.get(token)
        rejection: list[str] = []
        if route is None:
            rejection.append("logical_route_missing")
        else:
            if token.serializer != route.serializer:
                rejection.append("serializer_mismatch")
            if token.resource != route.resource:
                rejection.append("resource_mismatch")
            if binding.logical_resource != route.resource:
                rejection.append("projection_resource_mismatch")
            if tuple(attempt.endpoints) != route.endpoints:
                rejection.append("endpoint_manifest_mismatch")
        if frame is None:
            rejection.append("parent_frame_missing")
        elif bytes(attempt.payload) != frame.payload:
            rejection.append("payload_mismatch")
        if rejection:
            self._record(
                "ATTEMPT_BINDING_REJECTED",
                parent=token,
                detail=";".join(rejection) + ";state_unchanged",
            )
            raise ValueError(
                "TxAttempt does not match its immutable parent frame and route: "
                + ",".join(rejection)
            )

    @_serialized_mutation
    def start_host_write(
        self,
        attempt: TxAttempt,
        *,
        chip_select_id: str,
    ) -> StartAck:
        binding = self.projection.external_for_logical(
            attempt.token.frame.logical_channel
        )
        physical = binding.physical
        if physical is None:
            raise AssertionError("external projection unexpectedly lacks a physical target")
        if chip_select_id != self.bus.devices[physical.device_id]:
            self._record(
                "CHIP_SELECT_REJECTED",
                parent=attempt.token,
                detail=(
                    f"device={physical.device_id};expected="
                    f"{self.bus.devices[physical.device_id]};actual={chip_select_id};"
                    "state_unchanged"
                ),
            )
            raise ValueError("chip-select identity does not match the projected device")
        if self.device_in_reset[physical.device_id]:
            raise RuntimeError(f"physical bridge {physical.device_id} is in reset")
        if not self._parent_is_current_commit_pending(attempt):
            raise ValueError("parent attempt is not the current commit-pending token")
        self._validate_attempt_binding(attempt, binding)
        if any(
            transfer.parent == attempt.token
            and state
            in {
                TransferState.HOST_PENDING,
                TransferState.HOST_COMMITTED,
                TransferState.SERIALIZER_DONE_LATCHED,
            }
            for transfer, state in self.transfer_states.items()
        ):
            raise RuntimeError("parent attempt already has an open SPI transfer")

        transfer = SpiTransferToken(
            transfer_no=self._next_transfer_no,
            parent=attempt.token,
            bus_generation=self.bus.generation,
            device_generation=self.device_generation[physical.device_id],
            device_id=physical.device_id,
            physical_channel=physical.channel,
            logical_resource=binding.logical_resource,
            scenario_resource=binding.scenario_resource,
            chip_select_id=chip_select_id,
            payload_len=len(attempt.payload),
            payload_sha256=hashlib.sha256(attempt.payload).hexdigest().upper(),
        )
        if not self.bus.try_acquire(transfer):
            self._record(
                "BUS_BUSY",
                parent=attempt.token,
                detail=(
                    f"requested_device={physical.device_id};"
                    f"active_transfer={self.bus.active.transfer_no}"
                ),
            )
            return StartAck(StartStatus.BUS_BUSY, None)

        self._next_transfer_no += 1
        self.transfer_states[transfer] = TransferState.HOST_PENDING
        self._record(
            "HOST_TRANSFER_STARTED",
            transfer=transfer,
            detail=(
                f"device={transfer.device_id};channel={transfer.physical_channel};"
                f"cs={transfer.chip_select_id};payload_len={transfer.payload_len};"
                f"payload_sha256={transfer.payload_sha256}"
            ),
        )
        return StartAck(StartStatus.STARTED, transfer)

    @_serialized_mutation
    def resolve_host_transfer(
        self,
        transfer: SpiTransferToken,
        outcome: CommitResult,
    ) -> bool:
        try:
            outcome = CommitResult(outcome)
        except ValueError as exc:
            raise ValueError(f"unsupported host-transfer outcome: {outcome}") from exc

        state = self.transfer_states.get(transfer)
        current = self.bus.active
        stale_reasons: list[str] = []
        if state != TransferState.HOST_PENDING:
            stale_reasons.append(
                f"state={state.value if state is not None else 'UNKNOWN'}"
            )
        if current != transfer:
            stale_reasons.append("not_current_bus_owner")
        if transfer.bus_generation != self.bus.generation:
            stale_reasons.append("bus_generation_mismatch")
        if transfer.device_id not in self.device_generation:
            stale_reasons.append("unknown_device")
        else:
            if transfer.device_generation != self.device_generation[transfer.device_id]:
                stale_reasons.append("device_generation_mismatch")
            if self.device_in_reset[transfer.device_id]:
                stale_reasons.append("device_in_reset")
        if stale_reasons:
            self._record(
                "LATE_HOST_CALLBACK",
                transfer=transfer,
                detail=";".join(stale_reasons) + ";state_unchanged",
            )
            return False

        resolved = self.adapter.resolve_commit(transfer.parent, outcome)
        self.bus.release(transfer)
        if not resolved:
            self.transfer_states[transfer] = TransferState.UNCERTAIN
            self._record(
                "HOST_CALLBACK_REJECTED_BY_PARENT",
                transfer=transfer,
                detail="parent_state_unchanged_or_stale",
            )
            return False

        if outcome == CommitResult.NOT_COMMITTED:
            self.transfer_states[transfer] = TransferState.NOT_COMMITTED
            self._record(
                "HOST_NOT_COMMITTED",
                transfer=transfer,
                detail="frame_retained;fresh_parent_and_spi_token_required",
            )
        elif outcome == CommitResult.UNKNOWN:
            self.transfer_states[transfer] = TransferState.UNCERTAIN
            self._record(
                "HOST_COMMIT_UNKNOWN",
                transfer=transfer,
                detail="no_automatic_retry",
            )
        else:
            self.transfer_states[transfer] = TransferState.HOST_COMMITTED
            self.accepted_transfers.append(transfer)
            self._record(
                "ACCEPTED_BY_MODEL_SERIALIZER",
                transfer=transfer,
                detail="not_serializer_complete;not_connector_delivery",
            )
        return True

    @_serialized_mutation
    def mark_serializer_done(
        self,
        transfer: SpiTransferToken,
        *,
        source_device: str | None = None,
        source_channel: int | None = None,
        deliver_irq: bool = True,
    ) -> bool:
        device_id = source_device or transfer.device_id
        channel = transfer.physical_channel if source_channel is None else source_channel
        if device_id not in {"A", "B"}:
            self._record(
                "SERIALIZER_CALLBACK_REJECTED",
                transfer=transfer,
                detail=f"unknown_source_device={device_id};state_unchanged",
            )
            return False
        state = self.transfer_states.get(transfer)
        rejection: list[str] = []
        if state != TransferState.HOST_COMMITTED:
            rejection.append(f"state={state.value if state else 'UNKNOWN'}")
        if device_id != transfer.device_id:
            rejection.append("source_device_mismatch")
        if channel != transfer.physical_channel:
            rejection.append("source_channel_mismatch")
        if transfer.bus_generation != self.bus.generation:
            rejection.append("bus_generation_mismatch")
        if transfer.device_generation != self.device_generation[transfer.device_id]:
            rejection.append("device_generation_mismatch")
        if self.device_in_reset[transfer.device_id]:
            rejection.append("device_in_reset")
        if rejection:
            self._record(
                "LATE_SERIALIZER_CALLBACK",
                transfer=transfer,
                detail=";".join(rejection) + ";state_unchanged",
            )
            return False

        self.transfer_states[transfer] = TransferState.SERIALIZER_DONE_LATCHED
        self.completion_latches[transfer.device_id].append(transfer)
        if deliver_irq and self.irq_fault[transfer.device_id] != IrqFault.SUPPRESSED:
            self.irq_asserted[transfer.device_id] = True
        self._record(
            "MODEL_SERIALIZER_DONE_LATCHED",
            transfer=transfer,
            detail=(
                f"deliver_irq={str(deliver_irq).lower()};"
                f"irq_fault={self.irq_fault[transfer.device_id].value};"
                "not_connector_delivery"
            ),
        )
        return True

    def _remove_latch(self, transfer: SpiTransferToken) -> None:
        latches = self.completion_latches[transfer.device_id]
        if transfer in latches:
            latches.remove(transfer)
        if not latches and self.irq_fault[transfer.device_id] != IrqFault.STUCK_ASSERTED:
            self.irq_asserted[transfer.device_id] = False

    def _complete_latched(
        self,
        transfer: SpiTransferToken,
        *,
        evidence: str,
    ) -> bool:
        if self.transfer_states.get(transfer) != TransferState.SERIALIZER_DONE_LATCHED:
            self._record(
                "LATE_COMPLETION_SERVICE",
                transfer=transfer,
                detail="transfer_not_latched;state_unchanged",
            )
            return False
        stale_reasons: list[str] = []
        if not self.bus.online:
            stale_reasons.append("bus_offline")
        if transfer.bus_generation != self.bus.generation:
            stale_reasons.append("bus_generation_mismatch")
        if transfer.device_generation != self.device_generation[transfer.device_id]:
            stale_reasons.append("device_generation_mismatch")
        if self.device_in_reset[transfer.device_id]:
            stale_reasons.append("device_in_reset")
        if stale_reasons:
            marked = self.adapter.mark_uncertain(
                transfer.parent,
                "d_class_stale_completion;" + ";".join(stale_reasons),
            )
            self._remove_latch(transfer)
            self.transfer_states[transfer] = TransferState.UNCERTAIN
            self._record(
                "STALE_COMPLETION_QUARANTINED",
                transfer=transfer,
                detail=(
                    ";".join(stale_reasons)
                    + f";parent_marked_uncertain={str(marked).lower()};"
                    "no_automatic_retry"
                ),
            )
            return False
        completed = self.adapter.resolve_complete(
            transfer.parent,
            evidence=evidence,
        )
        self._remove_latch(transfer)
        if completed:
            self.transfer_states[transfer] = TransferState.COMPLETE
            self.completed_transfers.append(transfer)
            self._record(
                "MODEL_SERIALIZER_COMPLETED",
                transfer=transfer,
                detail=f"evidence={evidence};not_connector_delivery",
            )
            return True

        self.transfer_states[transfer] = TransferState.UNCERTAIN
        self._record(
            "COMPLETION_REJECTED_BY_PARENT",
            transfer=transfer,
            detail="state_uncertain_or_stale;not_connector_delivery",
        )
        return False

    @_serialized_mutation
    def service_irq(self, device_id: str) -> int:
        self._validate_device(device_id)
        if not self.irq_asserted[device_id]:
            self._record(
                "IRQ_SERVICE_WITHOUT_ASSERTION",
                detail=f"device={device_id};state_unchanged",
            )
            return 0

        latches = tuple(self.completion_latches[device_id])
        if not latches:
            self._record(
                "SPURIOUS_OR_STUCK_IRQ",
                detail=f"device={device_id};no_completion_latch",
            )
            if self.irq_fault[device_id] != IrqFault.STUCK_ASSERTED:
                self.irq_asserted[device_id] = False
            return 0

        completed = 0
        for transfer in latches:
            if self._complete_latched(
                transfer,
                evidence=f"MODEL_IRQ_{device_id}",
            ):
                completed += 1
        if self.irq_fault[device_id] == IrqFault.STUCK_ASSERTED:
            self.irq_asserted[device_id] = True
        return completed

    @_serialized_mutation
    def poll_completion(self, transfer: SpiTransferToken) -> bool:
        return self._complete_latched(
            transfer,
            evidence=f"MODEL_STATUS_POLL_{transfer.device_id}",
        )

    @_serialized_mutation
    def mark_irq_timeout(self, transfer: SpiTransferToken, *, reason: str) -> bool:
        if not reason.strip():
            raise ValueError("IRQ-timeout reason must be non-empty")
        state = self.transfer_states.get(transfer)
        if state not in {
            TransferState.HOST_COMMITTED,
            TransferState.SERIALIZER_DONE_LATCHED,
        }:
            self._record(
                "LATE_IRQ_TIMEOUT",
                transfer=transfer,
                detail=f"state={state.value if state else 'UNKNOWN'};state_unchanged",
            )
            return False
        marked = self.adapter.mark_uncertain(
            transfer.parent,
            f"d_class_irq_timeout;reason={reason}",
        )
        self._remove_latch(transfer)
        if marked:
            self.transfer_states[transfer] = TransferState.UNCERTAIN
            self._record(
                "IRQ_TIMEOUT_UNCERTAIN",
                transfer=transfer,
                detail=f"reason={reason};no_automatic_retry",
            )
        return marked

    @_serialized_mutation
    def set_irq_fault(
        self,
        device_id: str,
        fault: IrqFault,
        *,
        reason: str,
    ) -> None:
        self._validate_device(device_id)
        if not reason.strip():
            raise ValueError("IRQ-fault reason must be non-empty")
        fault = IrqFault(fault)
        self.irq_fault[device_id] = fault
        if fault == IrqFault.SUPPRESSED:
            self.irq_asserted[device_id] = False
        elif fault == IrqFault.STUCK_ASSERTED:
            self.irq_asserted[device_id] = True
        else:
            self.irq_asserted[device_id] = bool(
                self.completion_latches[device_id]
            )
        self._record(
            "IRQ_FAULT_STATE",
            detail=f"device={device_id};fault={fault.value};reason={reason}",
        )

    @_serialized_mutation
    def inject_spurious_irq(self, device_id: str, *, reason: str) -> None:
        self._validate_device(device_id)
        if not reason.strip():
            raise ValueError("spurious-IRQ reason must be non-empty")
        self.irq_asserted[device_id] = True
        self._record(
            "SPURIOUS_IRQ_INJECTED",
            detail=f"device={device_id};reason={reason}",
        )

    def _quarantine_open_transfers(self, reason: str) -> None:
        for transfer, state in tuple(self.transfer_states.items()):
            if state in {
                TransferState.HOST_PENDING,
                TransferState.HOST_COMMITTED,
                TransferState.SERIALIZER_DONE_LATCHED,
            }:
                self.transfer_states[transfer] = TransferState.UNCERTAIN
                self._record(
                    "TRANSFER_QUARANTINED",
                    transfer=transfer,
                    detail=f"reason={reason};no_automatic_retry",
                )
        self.bus.active = None
        for device_id in ("A", "B"):
            self.completion_latches[device_id].clear()
            self.irq_asserted[device_id] = (
                self.irq_fault[device_id] == IrqFault.STUCK_ASSERTED
            )

    @_serialized_mutation
    def inject_device_reset(self, device_id: str, *, reason: str) -> None:
        self._validate_device(device_id)
        if not reason.strip():
            raise ValueError("device-reset reason must be non-empty")
        self.adapter.force_safe_disabled(
            f"d_class_device_{device_id}_reset;reason={reason}"
        )
        for resource in self.projection.logical_resources_for_device(device_id):
            self.adapter.invalidate_resource(
                resource,
                reason=f"physical_device_{device_id}_reset",
            )
        self.device_generation[device_id] += 1
        self.device_in_reset[device_id] = True
        self._quarantine_open_transfers(f"device_{device_id}_reset")
        self._record(
            "DEVICE_RESET_INJECTED",
            detail=(
                f"device={device_id};generation={self.device_generation[device_id]};"
                f"policy={RESET_POLICY};reason={reason};"
                "electrical_isolation=UNKNOWN"
            ),
        )

    @_serialized_mutation
    def release_device_reset(self, device_id: str, *, reason: str) -> None:
        self._validate_device(device_id)
        if not reason.strip():
            raise ValueError("device-reset release reason must be non-empty")
        if not self.device_in_reset[device_id]:
            raise RuntimeError(f"physical bridge {device_id} is not in reset")
        self.device_in_reset[device_id] = False
        self._record(
            "DEVICE_RESET_RELEASED",
            detail=(
                f"device={device_id};generation={self.device_generation[device_id]};"
                f"reason={reason};explicit_config_and_idle_still_required"
            ),
        )

    @_serialized_mutation
    def inject_bus_reset(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("bus-reset reason must be non-empty")
        self.adapter.force_safe_disabled(f"d_class_bus_reset;reason={reason}")
        interrupted = self.bus.reset()
        for device_id in ("A", "B"):
            self.device_generation[device_id] += 1
            self.device_in_reset[device_id] = True
        external_resources = {
            binding.logical_resource
            for binding in self.projection.bindings.values()
            if binding.physical is not None
        }
        for resource in sorted(external_resources):
            self.adapter.invalidate_resource(resource, reason="shared_spi_bus_reset")
        self._quarantine_open_transfers("shared_spi_bus_reset")
        self._record(
            "BUS_RESET_INJECTED",
            transfer=interrupted,
            detail=(
                f"bus_generation={self.bus.generation};reason={reason};"
                "both_devices_require_recovery"
            ),
        )

    @_serialized_mutation
    def release_bus_reset(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("bus-reset release reason must be non-empty")
        self.bus.recover()
        self._record(
            "BUS_RESET_RELEASED",
            detail=(
                f"bus_generation={self.bus.generation};reason={reason};"
                "devices_and_resources_still_require_recovery"
            ),
        )

    def validate(self) -> RigValidation:
        anomalies: list[str] = []
        parent = self.adapter.validate()
        if parent.status != "TX_ADAPTER_MODEL_PASS":
            anomalies.append(f"PARENT_STATUS:{parent.status}")
        if self.bus.owner_id != self.OWNER_ID or self.bus.initialization_count != 1:
            anomalies.append(
                "BUS_OWNERSHIP_INVALID:"
                f"owner={self.bus.owner_id};init_count={self.bus.initialization_count}"
            )
        if set(self.bus.devices) != {"A", "B"}:
            anomalies.append(
                f"BUS_DEVICE_SET_INVALID:{sorted(self.bus.devices)}"
            )
        elif len(set(self.bus.devices.values())) != 2:
            anomalies.append("BUS_CHIP_SELECTS_NOT_UNIQUE")
        if self.bus.active is not None:
            anomalies.append(f"BUS_ACTIVE:{self.bus.active.transfer_no}")
        if not self.bus.online:
            anomalies.append("BUS_OFFLINE")
        for device_id in ("A", "B"):
            if self.device_in_reset[device_id]:
                anomalies.append(f"DEVICE_IN_RESET:{device_id}")
            if self.irq_fault[device_id] != IrqFault.NORMAL:
                anomalies.append(
                    f"IRQ_FAULT:{device_id}:{self.irq_fault[device_id].value}"
                )
            if self.completion_latches[device_id]:
                anomalies.append(
                    f"COMPLETION_LATCHED:{device_id}:"
                    f"count={len(self.completion_latches[device_id])}"
                )
            elif self.irq_asserted[device_id]:
                anomalies.append(f"IRQ_ASSERTED_WITHOUT_LATCH:{device_id}")
        for transfer, state in sorted(
            self.transfer_states.items(),
            key=lambda item: item[0].transfer_no,
        ):
            if state in {
                TransferState.HOST_PENDING,
                TransferState.HOST_COMMITTED,
                TransferState.SERIALIZER_DONE_LATCHED,
            }:
                anomalies.append(
                    f"TRANSFER_INCOMPLETE:{transfer.transfer_no}:{state.value}"
                )
            elif state == TransferState.UNCERTAIN:
                anomalies.append(f"TRANSFER_UNCERTAIN:{transfer.transfer_no}")

        accepted_set = set(self.accepted_transfers)
        completed_set = set(self.completed_transfers)
        if len(accepted_set) != len(self.accepted_transfers):
            anomalies.append("DUPLICATE_ACCEPTANCE_LEDGER_ENTRY")
        if len(completed_set) != len(self.completed_transfers):
            anomalies.append("DUPLICATE_COMPLETION_LEDGER_ENTRY")
        if not completed_set.issubset(accepted_set):
            anomalies.append("COMPLETION_WITHOUT_ACCEPTANCE")

        if any(state == TransferState.UNCERTAIN for state in self.transfer_states.values()):
            status = "D_DUAL_BRIDGE_MODEL_UNCERTAIN"
        elif any(
            state
            in {
                TransferState.HOST_PENDING,
                TransferState.HOST_COMMITTED,
                TransferState.SERIALIZER_DONE_LATCHED,
            }
            for state in self.transfer_states.values()
        ):
            status = "D_DUAL_BRIDGE_MODEL_INCOMPLETE"
        elif not self.completed_transfers:
            status = "D_DUAL_BRIDGE_MODEL_NOT_RUN"
            anomalies.append("NO_COMPLETED_EXTERNAL_TRANSFER")
        elif anomalies:
            status = "D_DUAL_BRIDGE_MODEL_NOT_READY"
        else:
            status = "D_DUAL_BRIDGE_MODEL_PASS"
        return RigValidation(
            status=status,
            anomalies=tuple(anomalies),
            accepted_by_serializer=len(self.accepted_transfers),
            completed_by_serializer=len(self.completed_transfers),
        )

    def event_digest(self) -> str:
        canonical = json.dumps(
            [asdict(event) for event in self.events],
            sort_keys=True,
            separators=(",", ":"),
            ensure_ascii=False,
        ).encode("utf-8")
        return hashlib.sha256(canonical).hexdigest().upper()
