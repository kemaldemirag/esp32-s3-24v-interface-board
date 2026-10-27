"""Exact-part-neutral fake SPI/loopback model for CF-09 source class A.

This ASSUMPTION / MODEL_ONLY prototype applies the token-safe transport
contract to the one-quad source allocation.  Unlike class D, the existing
``Cf09TransportAdapter(Topology.mirrored())`` route table already matches the
physical class-A allocation directly:

* L0 -> local UART1
* L1 -> local UART2
* L2 -> bridge A channel 0
* L3 -> bridge A channel 1
* L4 -> bridge A channel 2
* bridge A channel 3 -> dedicated RX reservation, never a TX target

The model has one symbolic SPI device and one symbolic chip-select identity.
It separates host transaction commit from serializer completion and exercises
retry, generation, reset and IRQ recovery contracts.  It does not select a
part or claim a register map, FIFO size, bus speed, IRQ polarity/latency,
reset waveform, UART timing, electrical isolation, power-off behaviour or
connector delivery.  Those remain UNKNOWN / NOT_RUN even when this model
passes.
"""

from __future__ import annotations

import hashlib
import json
from dataclasses import asdict, dataclass
from functools import wraps
from pathlib import Path
from threading import Lock
from types import MappingProxyType
from typing import Mapping

from cf09_d_dual_bridge_spi_model_v1 import (
    FakeSharedSpiBus,
    IrqFault,
    PhysicalUart,
    ProjectionBinding,
    RigEvent,
    SpiTransferToken,
    StartAck,
    StartStatus,
    TransferState,
)
from cf09_host_model_v1 import CommitResult, Topology, build_routes
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
MODEL_SCOPE = "A_CLASS_SINGLE_QUAD_SHARED_SPI_FAKE_LOOPBACK"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"
EXACT_PART_STATUS = "UNKNOWN / NOT_SELECTED"
CONNECTOR_DELIVERY_STATUS = "NOT_RUN / NOT_PROVEN"
RESET_POLICY = "GLOBAL_SAFE_DISABLE_BEFORE_RECONFIGURATION"
RX_RESERVATION = "BRIDGE_A_CH3"

DEFAULT_SCENARIO_PATH = (
    Path(__file__).resolve().parents[1]
    / "engineering"
    / "cf09-serial-source-scenarios-v1.0.json"
)


def _serialized_mutation(method):
    """Reject reentrant/concurrent harness mutations without partial state."""

    @wraps(method)
    def guarded(self, *args, **kwargs):
        if not self._mutation_lock.acquire(blocking=False):
            active = self._active_mutation or "another_thread"
            raise RuntimeError(
                f"concurrent/reentrant A-harness mutation rejected: active={active};"
                f"requested={method.__name__}"
            )
        self._active_mutation = method.__name__
        try:
            return method(self, *args, **kwargs)
        finally:
            self._active_mutation = None
            self._mutation_lock.release()

    return guarded


@dataclass(frozen=True)
class AClassValidation:
    status: str
    anomalies: tuple[str, ...]
    accepted_by_serializer: int
    completed_by_serializer: int
    hardware_status: str = HARDWARE_STATUS
    exact_part_status: str = EXACT_PART_STATUS
    connector_delivery_status: str = CONNECTOR_DELIVERY_STATUS


@dataclass(frozen=True)
class AdStructuralComparison:
    common_external_capacity: str
    a_external_device_count: int
    d_external_device_count: int
    a_symbolic_chip_select_count: int
    d_symbolic_chip_select_count: int
    a_route_binding: str
    d_route_binding: str
    a_reset_scope: str
    d_reset_scope: str
    independent_10_status: str
    evidence_boundary: str


def derive_a_d_structural_comparison(
    scenario_path: Path = DEFAULT_SCENARIO_PATH,
) -> AdStructuralComparison:
    """Derive source-count and model-structure facts; no hardware winner."""

    data = load_scenario_set(Path(scenario_path))
    scenarios = {item["scenario_id"]: item for item in data["scenarios"]}
    a = scenarios["A_2_UART_PLUS_QUAD"]
    d = scenarios["D_2_UART_PLUS_TWO_DUAL"]
    a_result = allocate_mirrored_5(a)
    d_result = allocate_mirrored_5(d)
    if a_result.status != AllocationStatus.PROVISIONAL:
        raise ValueError("class-A allocation is not provisionally available")
    if d_result.status != AllocationStatus.PROVISIONAL:
        raise ValueError("class-D allocation is not provisionally available")
    if len(a_result.tx) != 5 or len(d_result.tx) != 5:
        raise ValueError("A/D mirrored allocation no longer covers five TX channels")
    if a_result.rx.resource is None or d_result.rx.resource is None:
        raise ValueError("A/D dedicated RX allocation is missing")
    if a_result.rx.relationship != "DEDICATED_UART_CHANNEL":
        raise ValueError("class-A RX relationship is no longer dedicated")
    if d_result.rx.relationship != "DEDICATED_UART_CHANNEL":
        raise ValueError("class-D RX relationship is no longer dedicated")
    if not a_result.independent_10_status.startswith("BLOCKED_BY_RESOURCE_COUNT"):
        raise ValueError("class-A independent-10 boundary changed")
    if not d_result.independent_10_status.startswith("BLOCKED_BY_RESOURCE_COUNT"):
        raise ValueError("class-D independent-10 boundary changed")
    if a["external_tx_channels_in_mirrored_5"] != 3:
        raise ValueError("class-A external TX count changed")
    if d["external_tx_channels_in_mirrored_5"] != 3:
        raise ValueError("class-D external TX count changed")

    logical_resources = tuple(
        build_routes(Topology.mirrored())[index].resource for index in range(5)
    )
    a_resources = tuple(item.resource.resource_id for item in a_result.tx)
    d_resources = tuple(item.resource.resource_id for item in d_result.tx)
    if a_resources != logical_resources:
        raise ValueError("class-A allocation no longer directly matches logical routes")

    return AdStructuralComparison(
        common_external_capacity="3_TX_PLUS_1_DEDICATED_RX",
        a_external_device_count=len(a["external_devices"]),
        d_external_device_count=len(d["external_devices"]),
        a_symbolic_chip_select_count=len(a["external_devices"]),
        d_symbolic_chip_select_count=len(d["external_devices"]),
        a_route_binding="DIRECT_LOGICAL_TO_PHYSICAL_MATCH",
        d_route_binding=(
            "DIRECT_LOGICAL_TO_PHYSICAL_MATCH"
            if d_resources == logical_resources
            else "EXPLICIT_LOGICAL_TO_PHYSICAL_PROJECTION_REQUIRED"
        ),
        a_reset_scope="ONE_QUAD_DEVICE_COVERS_3_TX_AND_RESERVED_RX",
        d_reset_scope=(
            "TWO_PHYSICAL_DEVICES_BUT_GLOBAL_SAFE_DISABLE_UNTIL_"
            "ELECTRICAL_ISOLATION_IS_PROVEN"
        ),
        independent_10_status="BLOCKED_BY_RESOURCE_COUNT_FOR_A_AND_D",
        evidence_boundary="DERIVED / MODEL_ONLY / NO_HARDWARE_WINNER",
    )


class AClassRouteContract:
    """Validate that the logical mirrored routes directly equal class A."""

    EXPECTED_DESCRIPTORS = {
        0: (0, "S0", "UART1", None, None),
        1: (1, "S1", "UART2", None, None),
        2: (2, "S2", "BRIDGE_A_CH0", "A", 0),
        3: (3, "S3", "BRIDGE_A_CH1", "A", 1),
        4: (4, "S4", "BRIDGE_A_CH2", "A", 2),
    }

    _PHYSICAL_BY_RESOURCE = {
        "BRIDGE_A_CH0": PhysicalUart("A", 0),
        "BRIDGE_A_CH1": PhysicalUart("A", 1),
        "BRIDGE_A_CH2": PhysicalUart("A", 2),
    }

    def __init__(
        self,
        adapter: Cf09TransportAdapter,
        *,
        scenario_path: Path = DEFAULT_SCENARIO_PATH,
    ) -> None:
        if adapter.topology != Topology.mirrored():
            raise ValueError("class-A route contract requires mirrored topology")
        actual_descriptors = {
            channel: (
                route.logical_channel,
                route.serializer,
                route.resource,
                route.bridge,
                route.bridge_channel,
            )
            for channel, route in adapter.routes.items()
        }
        if actual_descriptors != self.EXPECTED_DESCRIPTORS:
            raise ValueError(
                "logical mirrored route descriptors no longer match class A: "
                f"{actual_descriptors}"
            )

        data = load_scenario_set(Path(scenario_path))
        scenarios = {item["scenario_id"]: item for item in data["scenarios"]}
        scenario = scenarios.get("A_2_UART_PLUS_QUAD")
        if scenario is None:
            raise ValueError("A_2_UART_PLUS_QUAD scenario is missing")
        allocation = allocate_mirrored_5(scenario)
        if allocation.status != AllocationStatus.PROVISIONAL:
            raise ValueError(
                "class-A allocation is not provisionally available: "
                f"{allocation.status.value}"
            )
        if allocation.rx.resource is None:
            raise ValueError("class-A dedicated RX reservation is missing")
        if allocation.rx.resource.resource_id != RX_RESERVATION:
            raise ValueError(
                "class-A RX reservation changed: "
                f"expected={RX_RESERVATION};actual={allocation.rx.resource.resource_id}"
            )

        bindings: dict[int, ProjectionBinding] = {}
        for tx in allocation.tx:
            route = adapter.routes[tx.logical_channel]
            if tx.resource.resource_id != route.resource:
                raise ValueError(
                    "class-A scenario is not a direct logical route match: "
                    f"channel={tx.logical_channel};scenario={tx.resource.resource_id};"
                    f"logical={route.resource}"
                )
            if tuple(tx.endpoints) != route.endpoints:
                raise ValueError(
                    "class-A endpoint manifest differs from logical transport: "
                    f"channel={tx.logical_channel}"
                )
            bindings[tx.logical_channel] = ProjectionBinding(
                logical_channel=tx.logical_channel,
                logical_resource=route.resource,
                scenario_resource=tx.resource.resource_id,
                physical=self._PHYSICAL_BY_RESOURCE.get(route.resource),
                endpoints=tuple(tx.endpoints),
            )
        if set(bindings) != set(range(5)):
            raise ValueError(f"class-A route contract is incomplete: {sorted(bindings)}")
        self.adapter = adapter
        self._bindings = MappingProxyType(dict(bindings))
        self.rx_reservation = allocation.rx.resource.resource_id
        self.scenario_path = Path(scenario_path)

    @property
    def bindings(self) -> Mapping[int, ProjectionBinding]:
        return self._bindings

    def external_for_logical(self, logical_channel: int) -> ProjectionBinding:
        if logical_channel not in self.bindings:
            raise ValueError(f"logical channel outside A contract: {logical_channel}")
        binding = self.bindings[logical_channel]
        if binding.physical is None:
            raise ValueError(
                f"logical channel {logical_channel} is local and has no SPI target"
            )
        return binding

    def logical_for_physical_tx(self, channel: int) -> int:
        if channel not in {0, 1, 2, 3}:
            raise ValueError(f"quad bridge channel outside 0..3: {channel}")
        if channel == 3:
            raise ValueError("BRIDGE_A_CH3 is reserved for RX and cannot be a TX target")
        for logical_channel, binding in self.bindings.items():
            if binding.physical == PhysicalUart("A", channel):
                return logical_channel
        raise ValueError(f"quad bridge channel is not allocated for TX: {channel}")

    @property
    def external_logical_resources(self) -> tuple[str, ...]:
        return tuple(
            binding.logical_resource
            for binding in self.bindings.values()
            if binding.physical is not None
        )

    def current_route_anomalies(self) -> tuple[str, ...]:
        """Detect any descriptor or endpoint drift after construction."""

        anomalies: list[str] = []
        if set(self.bindings) != set(self.EXPECTED_DESCRIPTORS):
            anomalies.append(
                "BINDING_CHANNEL_SET_DRIFT:"
                f"actual={sorted(self.bindings)}"
            )
        if set(self.adapter.routes) != set(self.EXPECTED_DESCRIPTORS):
            anomalies.append(
                "ROUTE_CHANNEL_SET_DRIFT:"
                f"actual={sorted(self.adapter.routes)}"
            )
        for channel, expected in self.EXPECTED_DESCRIPTORS.items():
            binding = self.bindings.get(channel)
            expected_resource = expected[2]
            expected_binding = (
                channel,
                expected_resource,
                expected_resource,
                self._PHYSICAL_BY_RESOURCE.get(expected_resource),
                (f"TTL{channel}", f"RS232{channel}"),
            )
            if binding is None:
                anomalies.append(f"BINDING_MISSING:channel={channel}")
            else:
                actual_binding = (
                    binding.logical_channel,
                    binding.logical_resource,
                    binding.scenario_resource,
                    binding.physical,
                    binding.endpoints,
                )
                if actual_binding != expected_binding:
                    anomalies.append(
                        f"BINDING_DRIFT:channel={channel}:"
                        f"expected={expected_binding}:actual={actual_binding}"
                    )
            route = self.adapter.routes.get(channel)
            if route is None:
                anomalies.append(f"ROUTE_MISSING:channel={channel}")
                continue
            actual = (
                route.logical_channel,
                route.serializer,
                route.resource,
                route.bridge,
                route.bridge_channel,
            )
            if actual != expected:
                anomalies.append(
                    f"ROUTE_DESCRIPTOR_DRIFT:channel={channel}:"
                    f"expected={expected}:actual={actual}"
                )
            if binding is not None and route.endpoints != binding.endpoints:
                anomalies.append(
                    f"ROUTE_ENDPOINT_DRIFT:channel={channel}:"
                    f"expected={binding.endpoints}:actual={route.endpoints}"
                )
        return tuple(anomalies)


class AClassSingleQuadHarness:
    """Token-safe one-quad fake SPI driver with IRQ/reset fault injection."""

    OWNER_ID = "CF09_A_CLASS_MODEL_OWNER"
    DEVICE_ID = "A"

    def __init__(
        self,
        adapter: Cf09TransportAdapter,
        *,
        scenario_path: Path = DEFAULT_SCENARIO_PATH,
        chip_select_id: str = "MODEL_CS_A",
    ) -> None:
        self.adapter = adapter
        self.routes = AClassRouteContract(adapter, scenario_path=scenario_path)
        self.bus = FakeSharedSpiBus()
        self.bus.initialize(self.OWNER_ID)
        self.bus.attach_device(self.OWNER_ID, self.DEVICE_ID, chip_select_id)
        self.expected_chip_select_id = chip_select_id
        self.device_generation = 0
        self.device_in_reset = False
        self.irq_fault = IrqFault.NORMAL
        self.irq_asserted = False
        self.completion_latches: list[SpiTransferToken] = []
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
        event = RigEvent(self._tick, kind, transfer, parent, detail)
        self.events.append(event)
        return event

    def _parent_is_current_commit_pending(self, attempt: TxAttempt) -> bool:
        token = attempt.token
        return (
            self.adapter.attempt_states.get(token) == AttemptState.COMMIT_PENDING
            and self.adapter.pending_by_serializer.get(token.serializer) == token
        )

    def _bus_contract_anomalies(self) -> tuple[str, ...]:
        anomalies: list[str] = []
        if self.bus.owner_id != self.OWNER_ID:
            anomalies.append(
                f"bus_owner_mismatch:expected={self.OWNER_ID}:actual={self.bus.owner_id}"
            )
        if self.bus.initialization_count != 1:
            anomalies.append(
                f"bus_init_count_mismatch:actual={self.bus.initialization_count}"
            )
        expected_devices = {self.DEVICE_ID: self.expected_chip_select_id}
        if self.bus.devices != expected_devices:
            anomalies.append(
                f"bus_device_registry_drift:expected={expected_devices}:"
                f"actual={self.bus.devices}"
            )
        return tuple(anomalies)

    def _validate_attempt_binding(
        self,
        attempt: TxAttempt,
        binding: ProjectionBinding,
    ) -> None:
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
                rejection.append("route_contract_resource_mismatch")
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
        binding = self.routes.external_for_logical(
            attempt.token.frame.logical_channel
        )
        physical = binding.physical
        if physical is None:
            raise AssertionError("external class-A binding lacks a physical target")
        route_anomalies = self.routes.current_route_anomalies()
        if route_anomalies:
            self._record(
                "ROUTE_CONTRACT_REJECTED",
                parent=attempt.token,
                detail=";".join(route_anomalies) + ";state_unchanged",
            )
            raise RuntimeError("class-A route contract drifted after construction")
        bus_anomalies = self._bus_contract_anomalies()
        if bus_anomalies:
            self._record(
                "BUS_CONTRACT_REJECTED",
                parent=attempt.token,
                detail=";".join(bus_anomalies) + ";state_unchanged",
            )
            raise RuntimeError("class-A SPI bus contract drifted")
        expected_cs = self.expected_chip_select_id
        if chip_select_id != expected_cs:
            self._record(
                "CHIP_SELECT_REJECTED",
                parent=attempt.token,
                detail=(
                    f"expected={expected_cs};actual={chip_select_id};"
                    "state_unchanged"
                ),
            )
            raise ValueError("chip-select identity does not match class-A device")
        if self.device_in_reset:
            raise RuntimeError("class-A quad bridge is in reset")
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
            device_generation=self.device_generation,
            device_id=self.DEVICE_ID,
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
                detail=f"active_transfer={self.bus.active.transfer_no}",
            )
            return StartAck(StartStatus.BUS_BUSY, None)
        self._next_transfer_no += 1
        self.transfer_states[transfer] = TransferState.HOST_PENDING
        self._record(
            "HOST_TRANSFER_STARTED",
            transfer=transfer,
            detail=(
                f"channel={transfer.physical_channel};cs={transfer.chip_select_id};"
                f"payload_len={transfer.payload_len};"
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
        stale_reasons: list[str] = []
        if state != TransferState.HOST_PENDING:
            stale_reasons.append(f"state={state.value if state else 'UNKNOWN'}")
        if self.bus.active != transfer:
            stale_reasons.append("not_current_bus_owner")
        if not self.bus.online:
            stale_reasons.append("bus_offline")
        stale_reasons.extend(self._bus_contract_anomalies())
        if transfer.bus_generation != self.bus.generation:
            stale_reasons.append("bus_generation_mismatch")
        if transfer.device_id != self.DEVICE_ID:
            stale_reasons.append("unknown_device")
        else:
            if transfer.device_generation != self.device_generation:
                stale_reasons.append("device_generation_mismatch")
            if self.device_in_reset:
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
        source_channel: int | None = None,
        deliver_irq: bool = True,
    ) -> bool:
        channel = transfer.physical_channel if source_channel is None else source_channel
        state = self.transfer_states.get(transfer)
        rejection: list[str] = []
        if state != TransferState.HOST_COMMITTED:
            rejection.append(f"state={state.value if state else 'UNKNOWN'}")
        if transfer.device_id != self.DEVICE_ID:
            rejection.append("source_device_mismatch")
        if not self.bus.online:
            rejection.append("bus_offline")
        rejection.extend(self._bus_contract_anomalies())
        if channel != transfer.physical_channel:
            rejection.append("source_channel_mismatch")
        if transfer.bus_generation != self.bus.generation:
            rejection.append("bus_generation_mismatch")
        if transfer.device_generation != self.device_generation:
            rejection.append("device_generation_mismatch")
        if self.device_in_reset:
            rejection.append("device_in_reset")
        if rejection:
            self._record(
                "LATE_SERIALIZER_CALLBACK",
                transfer=transfer,
                detail=";".join(rejection) + ";state_unchanged",
            )
            return False
        self.transfer_states[transfer] = TransferState.SERIALIZER_DONE_LATCHED
        self.completion_latches.append(transfer)
        if deliver_irq and self.irq_fault != IrqFault.SUPPRESSED:
            self.irq_asserted = True
        self._record(
            "MODEL_SERIALIZER_DONE_LATCHED",
            transfer=transfer,
            detail=(
                f"deliver_irq={str(deliver_irq).lower()};"
                f"irq_fault={self.irq_fault.value};not_connector_delivery"
            ),
        )
        return True

    def _remove_latch(self, transfer: SpiTransferToken) -> None:
        if transfer in self.completion_latches:
            self.completion_latches.remove(transfer)
        if not self.completion_latches and self.irq_fault != IrqFault.STUCK_ASSERTED:
            self.irq_asserted = False

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
        stale_reasons.extend(self._bus_contract_anomalies())
        if transfer.bus_generation != self.bus.generation:
            stale_reasons.append("bus_generation_mismatch")
        if transfer.device_generation != self.device_generation:
            stale_reasons.append("device_generation_mismatch")
        if self.device_in_reset:
            stale_reasons.append("device_in_reset")
        if stale_reasons:
            marked = self.adapter.mark_uncertain(
                transfer.parent,
                "a_class_stale_completion;" + ";".join(stale_reasons),
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
    def service_irq(self) -> int:
        if not self.irq_asserted:
            self._record("IRQ_SERVICE_WITHOUT_ASSERTION", detail="state_unchanged")
            return 0
        latches = tuple(self.completion_latches)
        if not latches:
            self._record("SPURIOUS_OR_STUCK_IRQ", detail="no_completion_latch")
            if self.irq_fault != IrqFault.STUCK_ASSERTED:
                self.irq_asserted = False
            return 0
        completed = 0
        for transfer in latches:
            if self._complete_latched(transfer, evidence="MODEL_IRQ_A"):
                completed += 1
        if self.irq_fault == IrqFault.STUCK_ASSERTED:
            self.irq_asserted = True
        return completed

    @_serialized_mutation
    def poll_completion(self, transfer: SpiTransferToken) -> bool:
        return self._complete_latched(transfer, evidence="MODEL_STATUS_POLL_A")

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
            f"a_class_irq_timeout;reason={reason}",
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
    def set_irq_fault(self, fault: IrqFault, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("IRQ-fault reason must be non-empty")
        fault = IrqFault(fault)
        self.irq_fault = fault
        if fault == IrqFault.SUPPRESSED:
            self.irq_asserted = False
        elif fault == IrqFault.STUCK_ASSERTED:
            self.irq_asserted = True
        else:
            self.irq_asserted = bool(self.completion_latches)
        self._record(
            "IRQ_FAULT_STATE",
            detail=f"fault={fault.value};reason={reason}",
        )

    @_serialized_mutation
    def inject_spurious_irq(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("spurious-IRQ reason must be non-empty")
        self.irq_asserted = True
        self._record("SPURIOUS_IRQ_INJECTED", detail=f"reason={reason}")

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
        self.completion_latches.clear()
        self.irq_asserted = self.irq_fault == IrqFault.STUCK_ASSERTED

    @_serialized_mutation
    def inject_device_reset(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("device-reset reason must be non-empty")
        self.adapter.force_safe_disabled(f"a_class_device_reset;reason={reason}")
        self.adapter.set_bridge_state(
            "A",
            up=False,
            reason=f"a_class_quad_reset;{reason}",
        )
        self.device_generation += 1
        self.device_in_reset = True
        self._quarantine_open_transfers("a_class_quad_reset")
        self._record(
            "DEVICE_RESET_INJECTED",
            detail=(
                f"generation={self.device_generation};policy={RESET_POLICY};"
                f"reason={reason};electrical_behaviour=UNKNOWN"
            ),
        )

    @_serialized_mutation
    def release_device_reset(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("device-reset release reason must be non-empty")
        if not self.device_in_reset:
            raise RuntimeError("class-A quad bridge is not in reset")
        self.device_in_reset = False
        self.adapter.set_bridge_state(
            "A",
            up=True,
            reason=f"a_class_quad_reset_released;{reason}",
        )
        self._record(
            "DEVICE_RESET_RELEASED",
            detail=(
                f"generation={self.device_generation};reason={reason};"
                "explicit_config_and_idle_still_required"
            ),
        )

    @_serialized_mutation
    def inject_bus_reset(self, *, reason: str) -> None:
        if not reason.strip():
            raise ValueError("bus-reset reason must be non-empty")
        self.adapter.force_safe_disabled(f"a_class_bus_reset;reason={reason}")
        interrupted = self.bus.reset()
        self.adapter.set_bridge_state(
            "A",
            up=False,
            reason=f"a_class_bus_reset;{reason}",
        )
        self.device_generation += 1
        self.device_in_reset = True
        self._quarantine_open_transfers("a_class_bus_reset")
        self._record(
            "BUS_RESET_INJECTED",
            transfer=interrupted,
            detail=(
                f"bus_generation={self.bus.generation};"
                f"device_generation={self.device_generation};reason={reason};"
                "device_and_resources_require_recovery"
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
                "device_and_resources_still_require_recovery"
            ),
        )

    def validate(self) -> AClassValidation:
        anomalies: list[str] = []
        parent = self.adapter.validate()
        if parent.status != "TX_ADAPTER_MODEL_PASS":
            anomalies.append(f"PARENT_STATUS:{parent.status}")
        anomalies.extend(self.routes.current_route_anomalies())
        anomalies.extend(self._bus_contract_anomalies())
        if self.bus.active is not None:
            anomalies.append(f"BUS_ACTIVE:{self.bus.active.transfer_no}")
        if not self.bus.online:
            anomalies.append("BUS_OFFLINE")
        if self.device_in_reset:
            anomalies.append("DEVICE_IN_RESET:A")
        if self.irq_fault != IrqFault.NORMAL:
            anomalies.append(f"IRQ_FAULT:A:{self.irq_fault.value}")
        if self.completion_latches:
            anomalies.append(
                f"COMPLETION_LATCHED:A:count={len(self.completion_latches)}"
            )
        elif self.irq_asserted:
            anomalies.append("IRQ_ASSERTED_WITHOUT_LATCH:A")
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
            status = "A_SINGLE_QUAD_MODEL_UNCERTAIN"
        elif any(
            state
            in {
                TransferState.HOST_PENDING,
                TransferState.HOST_COMMITTED,
                TransferState.SERIALIZER_DONE_LATCHED,
            }
            for state in self.transfer_states.values()
        ):
            status = "A_SINGLE_QUAD_MODEL_INCOMPLETE"
        elif not self.completed_transfers:
            status = "A_SINGLE_QUAD_MODEL_NOT_RUN"
            anomalies.append("NO_COMPLETED_EXTERNAL_TRANSFER")
        elif anomalies:
            status = "A_SINGLE_QUAD_MODEL_NOT_READY"
        else:
            status = "A_SINGLE_QUAD_MODEL_PASS"
        return AClassValidation(
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
