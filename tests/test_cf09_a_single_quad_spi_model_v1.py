"""Contract and A-D parity tests for the CF-09 class-A single-quad fake."""

from __future__ import annotations

import unittest
from dataclasses import replace

from cf09_a_single_quad_spi_model_v1 import (
    CONNECTOR_DELIVERY_STATUS,
    EXACT_PART_STATUS,
    HARDWARE_STATUS,
    MODEL_CLASS,
    RESET_POLICY,
    RX_RESERVATION,
    AClassSingleQuadHarness,
    derive_a_d_structural_comparison,
)
from cf09_d_dual_bridge_spi_model_v1 import (
    DClassDualBridgeHarness,
    IrqFault,
    PhysicalUart,
    StartStatus,
    TransferState,
)
from cf09_host_model_v1 import Bank, BankState, CommitResult, Topology
from cf09_transport_adapter_v1 import (
    AttemptState,
    Cf09TransportAdapter,
    TxAttempt,
)


def ready_adapter(session_id: str) -> Cf09TransportAdapter:
    adapter = Cf09TransportAdapter(
        Topology.mirrored(),
        session_id=session_id,
    )
    adapter.set_bridge_state("A", up=True, reason="MODEL_ONLY_AGGREGATE_READY")
    adapter.declare_all_resources_ready()
    adapter.arm_outputs()
    return adapter


def ready_a(
    *,
    session_id: str = "A-CLASS-TEST",
    chip_select_id: str = "MODEL_CS_A",
) -> tuple[Cf09TransportAdapter, AClassSingleQuadHarness]:
    adapter = ready_adapter(session_id)
    return adapter, AClassSingleQuadHarness(
        adapter,
        chip_select_id=chip_select_id,
    )


def begin_a(
    adapter: Cf09TransportAdapter,
    harness: AClassSingleQuadHarness,
    logical_channel: int,
    payload: bytes,
):
    adapter.enqueue(logical_channel, payload)
    attempt = adapter.begin_next(logical_channel)
    if attempt is None:
        raise AssertionError("class-A external attempt unexpectedly unavailable")
    ack = harness.start_host_write(
        attempt,
        chip_select_id=harness.bus.devices["A"],
    )
    if ack.status != StartStatus.STARTED or ack.transfer is None:
        raise AssertionError("class-A fake SPI transfer failed to start")
    return attempt, ack.transfer


def complete_a(
    adapter: Cf09TransportAdapter,
    harness: AClassSingleQuadHarness,
    logical_channel: int,
    payload: bytes,
):
    attempt, transfer = begin_a(adapter, harness, logical_channel, payload)
    if not harness.resolve_host_transfer(transfer, CommitResult.COMMITTED):
        raise AssertionError("class-A fake host commit failed")
    if not harness.mark_serializer_done(transfer):
        raise AssertionError("class-A fake serializer completion failed")
    if harness.service_irq() != 1:
        raise AssertionError("class-A fake IRQ did not complete one transfer")
    return attempt, transfer


class AClassRouteAndStructureTests(unittest.TestCase):
    def test_route_bindings_are_read_only_frozen_contract_values(self) -> None:
        _, harness = ready_a()
        forged = replace(
            harness.routes.bindings[2],
            physical=PhysicalUart("A", 2),
        )
        with self.assertRaises(TypeError):
            harness.routes.bindings[2] = forged
        with self.assertRaises(AttributeError):
            harness.routes.bindings = {2: forged}
        self.assertEqual(harness.routes.current_route_anomalies(), ())

    def test_constructor_rejects_bridge_channel_descriptor_drift(self) -> None:
        adapter = ready_adapter("A-DESCRIPTOR-DRIFT")
        adapter.routes[2] = replace(adapter.routes[2], bridge_channel=3)
        with self.assertRaisesRegex(ValueError, "descriptors"):
            AClassSingleQuadHarness(adapter)

    def test_constructor_rejects_route_internal_logical_channel_drift(self) -> None:
        adapter = ready_adapter("A-LOGICAL-CHANNEL-DRIFT")
        adapter.routes[2] = replace(adapter.routes[2], logical_channel=3)
        with self.assertRaisesRegex(ValueError, "descriptors"):
            AClassSingleQuadHarness(adapter)

    def test_runtime_route_drift_blocks_start_and_prevents_pass(self) -> None:
        adapter, harness = ready_a()
        complete_a(adapter, harness, 2, b"valid-before-drift")
        adapter.routes[2] = replace(adapter.routes[2], bridge_channel=3)
        validation = harness.validate()
        self.assertEqual(validation.status, "A_SINGLE_QUAD_MODEL_NOT_READY")
        self.assertTrue(
            any(
                item.startswith("ROUTE_DESCRIPTOR_DRIFT:channel=2")
                for item in validation.anomalies
            )
        )

        adapter.enqueue(2, b"blocked-after-drift")
        attempt = adapter.begin_next(2)
        with self.assertRaisesRegex(RuntimeError, "route contract drifted"):
            harness.start_host_write(attempt, chip_select_id="MODEL_CS_A")
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.COMMIT_PENDING,
        )

    def test_a_routes_directly_match_single_quad_and_ch3_is_rx_only(self) -> None:
        adapter, harness = ready_a()
        self.assertEqual(
            [harness.routes.bindings[index].scenario_resource for index in range(5)],
            [
                "UART1",
                "UART2",
                "BRIDGE_A_CH0",
                "BRIDGE_A_CH1",
                "BRIDGE_A_CH2",
            ],
        )
        self.assertEqual(
            [adapter.routes[index].resource for index in range(5)],
            [
                "UART1",
                "UART2",
                "BRIDGE_A_CH0",
                "BRIDGE_A_CH1",
                "BRIDGE_A_CH2",
            ],
        )
        self.assertEqual(harness.routes.rx_reservation, RX_RESERVATION)
        with self.assertRaisesRegex(ValueError, "reserved for RX"):
            harness.routes.logical_for_physical_tx(3)
        self.assertEqual(
            [harness.routes.logical_for_physical_tx(index) for index in range(3)],
            [2, 3, 4],
        )

    def test_a_d_structural_comparison_has_no_hardware_winner(self) -> None:
        comparison = derive_a_d_structural_comparison()
        self.assertEqual(comparison.common_external_capacity, "3_TX_PLUS_1_DEDICATED_RX")
        self.assertEqual(comparison.a_external_device_count, 1)
        self.assertEqual(comparison.d_external_device_count, 2)
        self.assertEqual(comparison.a_symbolic_chip_select_count, 1)
        self.assertEqual(comparison.d_symbolic_chip_select_count, 2)
        self.assertEqual(
            comparison.a_route_binding,
            "DIRECT_LOGICAL_TO_PHYSICAL_MATCH",
        )
        self.assertEqual(
            comparison.independent_10_status,
            "BLOCKED_BY_RESOURCE_COUNT_FOR_A_AND_D",
        )
        self.assertEqual(
            comparison.evidence_boundary,
            "DERIVED / MODEL_ONLY / NO_HARDWARE_WINNER",
        )

    def test_single_bus_owner_device_and_symbolic_chip_select_are_explicit(self) -> None:
        _, harness = ready_a()
        self.assertEqual(harness.bus.initialization_count, 1)
        self.assertEqual(harness.bus.owner_id, harness.OWNER_ID)
        self.assertEqual(harness.bus.devices, {"A": "MODEL_CS_A"})
        with self.assertRaisesRegex(RuntimeError, "already initialized"):
            harness.bus.initialize("SECOND_OWNER")

        harness.bus.attach_device(harness.OWNER_ID, "B", "MODEL_CS_B")
        validation = harness.validate()
        self.assertEqual(validation.status, "A_SINGLE_QUAD_MODEL_NOT_RUN")
        self.assertTrue(
            any(
                item.startswith("bus_device_registry_drift")
                for item in validation.anomalies
            )
        )

    def test_local_uart_and_wrong_chip_select_fail_closed(self) -> None:
        adapter, harness = ready_a()
        adapter.enqueue(0, b"local")
        local = adapter.begin_next(0)
        with self.assertRaisesRegex(ValueError, "local"):
            harness.start_host_write(local, chip_select_id="MODEL_CS_A")
        self.assertIsNone(harness.bus.active)

        adapter.enqueue(2, b"external")
        external = adapter.begin_next(2)
        with self.assertRaisesRegex(ValueError, "chip-select"):
            harness.start_host_write(external, chip_select_id="MODEL_WRONG_CS")
        self.assertIsNone(harness.bus.active)
        self.assertEqual(
            adapter.attempt_states[external.token],
            AttemptState.COMMIT_PENDING,
        )

    def test_missing_bus_device_registry_is_audited_and_fail_closed(self) -> None:
        adapter, harness = ready_a()
        adapter.enqueue(2, b"registry")
        attempt = adapter.begin_next(2)
        del harness.bus.devices["A"]
        with self.assertRaisesRegex(RuntimeError, "bus contract drifted"):
            harness.start_host_write(attempt, chip_select_id="MODEL_CS_A")
        self.assertIsNone(harness.bus.active)
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.COMMIT_PENDING,
        )
        self.assertEqual(harness.events[-1].kind, "BUS_CONTRACT_REJECTED")
        self.assertIn("bus_device_registry_drift", harness.events[-1].detail)

    def test_chip_select_registry_drift_blocks_callbacks_and_completion(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 2, b"cs-drift")
        harness.bus.devices["A"] = "OTHER_CS"
        self.assertFalse(
            harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.COMMIT_PENDING,
        )
        self.assertTrue(
            any(
                "bus_device_registry_drift" in item
                for item in harness.validate().anomalies
            )
        )

        harness.bus.devices["A"] = "MODEL_CS_A"
        self.assertTrue(
            harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        self.assertTrue(harness.mark_serializer_done(transfer))
        harness.bus.devices["A"] = "OTHER_CS"
        self.assertEqual(harness.service_irq(), 0)
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.UNCERTAIN,
        )
        self.assertEqual(adapter.observations, [])


class AClassCommitAndRetryTests(unittest.TestCase):
    def test_three_external_channels_share_one_nonoverlapping_bus(self) -> None:
        adapter, harness = ready_a()
        adapter.enqueue(2, b"ch0")
        adapter.enqueue(3, b"ch1")
        first = adapter.begin_next(2)
        second = adapter.begin_next(3)
        first_ack = harness.start_host_write(first, chip_select_id="MODEL_CS_A")
        second_ack = harness.start_host_write(second, chip_select_id="MODEL_CS_A")
        self.assertEqual(first_ack.status, StartStatus.STARTED)
        self.assertEqual(second_ack.status, StartStatus.BUS_BUSY)
        self.assertEqual(harness.bus.active.physical_channel, 0)
        harness.resolve_host_transfer(first_ack.transfer, CommitResult.NOT_COMMITTED)

        second_ack = harness.start_host_write(second, chip_select_id="MODEL_CS_A")
        self.assertEqual(second_ack.status, StartStatus.STARTED)
        self.assertEqual(second_ack.transfer.physical_channel, 1)
        self.assertEqual(second_ack.transfer.transfer_no, 2)
        harness.resolve_host_transfer(second_ack.transfer, CommitResult.NOT_COMMITTED)
        self.assertIsNone(harness.bus.active)

    def test_host_commit_is_distinct_from_serializer_and_connector_completion(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 2, b"phase-boundary")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.INFLIGHT)
        self.assertEqual(adapter.observations, [])
        self.assertEqual(harness.transfer_states[transfer], TransferState.HOST_COMMITTED)

        harness.mark_serializer_done(transfer)
        self.assertEqual(adapter.observations, [])
        self.assertEqual(
            harness.transfer_states[transfer],
            TransferState.SERIALIZER_DONE_LATCHED,
        )
        self.assertEqual(harness.service_irq(), 1)
        self.assertEqual(
            {item.endpoint for item in adapter.observations},
            {"TTL2", "RS2322"},
        )
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")
        self.assertEqual(
            harness.validate().connector_delivery_status,
            CONNECTOR_DELIVERY_STATUS,
        )

    def test_all_three_quad_channels_can_complete_in_reverse_order(self) -> None:
        adapter, harness = ready_a()
        transfers = []
        for logical_channel in (2, 3, 4):
            _, transfer = begin_a(
                adapter,
                harness,
                logical_channel,
                f"channel-{logical_channel}".encode("ascii"),
            )
            harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
            transfers.append(transfer)
        for transfer in reversed(transfers):
            harness.mark_serializer_done(transfer)
        self.assertEqual(harness.service_irq(), 3)
        self.assertEqual(
            [item.physical_channel for item in harness.completed_transfers],
            [2, 1, 0],
        )
        self.assertEqual(
            {item.endpoint for item in adapter.observations},
            {"TTL2", "RS2322", "TTL3", "RS2323", "TTL4", "RS2324"},
        )
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_forged_payload_or_endpoint_manifest_is_rejected(self) -> None:
        adapter, harness = ready_a()
        adapter.enqueue(2, b"original")
        attempt = adapter.begin_next(2)
        forged_payload = TxAttempt(attempt.token, b"forged", attempt.endpoints)
        with self.assertRaisesRegex(ValueError, "payload_mismatch"):
            harness.start_host_write(
                forged_payload,
                chip_select_id="MODEL_CS_A",
            )
        forged_endpoints = TxAttempt(attempt.token, attempt.payload, ("TTL2",))
        with self.assertRaisesRegex(ValueError, "endpoint_manifest_mismatch"):
            harness.start_host_write(
                forged_endpoints,
                chip_select_id="MODEL_CS_A",
            )
        self.assertIsNone(harness.bus.active)

    def test_malformed_callback_device_is_audited_and_rejected(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 2, b"malformed")
        malformed = replace(transfer, device_id="B")
        self.assertFalse(
            harness.resolve_host_transfer(malformed, CommitResult.COMMITTED)
        )
        self.assertEqual(harness.bus.active, transfer)
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.COMMIT_PENDING)
        self.assertIn("unknown_device", harness.events[-1].detail)
        harness.resolve_host_transfer(transfer, CommitResult.NOT_COMMITTED)

    def test_not_committed_requires_fresh_parent_and_spi_tokens(self) -> None:
        adapter, harness = ready_a()
        first_attempt, first_transfer = begin_a(adapter, harness, 2, b"retry")
        harness.resolve_host_transfer(first_transfer, CommitResult.NOT_COMMITTED)
        second_attempt = adapter.begin_next(2)
        self.assertGreater(second_attempt.token.attempt_no, first_attempt.token.attempt_no)
        second_transfer = harness.start_host_write(
            second_attempt,
            chip_select_id="MODEL_CS_A",
        ).transfer
        self.assertNotEqual(first_transfer, second_transfer)
        self.assertFalse(
            harness.resolve_host_transfer(first_transfer, CommitResult.COMMITTED)
        )
        harness.resolve_host_transfer(second_transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(second_transfer)
        self.assertEqual(harness.service_irq(), 1)
        self.assertEqual(len(adapter.observations), 2)
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_unknown_commit_is_uncertain_and_never_auto_retried(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 4, b"ambiguous")
        harness.resolve_host_transfer(transfer, CommitResult.UNKNOWN)
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.pending_count(), 0)
        self.assertIsNone(adapter.begin_next(4))
        self.assertFalse(harness.mark_serializer_done(transfer))
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_UNCERTAIN")


class AClassIrqResetTests(unittest.TestCase):
    def test_wrong_channel_and_duplicate_completion_do_not_duplicate_delivery(self) -> None:
        adapter, harness = ready_a()
        _, transfer = begin_a(adapter, harness, 2, b"binding")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        self.assertFalse(harness.mark_serializer_done(transfer, source_channel=1))
        self.assertEqual(harness.service_irq(), 0)
        self.assertTrue(harness.mark_serializer_done(transfer))
        self.assertEqual(harness.service_irq(), 1)
        self.assertFalse(harness.mark_serializer_done(transfer))
        self.assertEqual(harness.service_irq(), 0)
        self.assertEqual(len(adapter.observations), 2)

    def test_suppressed_irq_uses_explicit_status_poll_fallback(self) -> None:
        adapter, harness = ready_a()
        harness.set_irq_fault(IrqFault.SUPPRESSED, reason="MODEL_DROP_IRQ")
        _, transfer = begin_a(adapter, harness, 4, b"poll")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(transfer)
        self.assertFalse(harness.irq_asserted)
        self.assertEqual(harness.service_irq(), 0)
        self.assertTrue(harness.poll_completion(transfer))
        harness.set_irq_fault(IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_irq_timeout_is_uncertain_and_late_completion_is_ignored(self) -> None:
        adapter, harness = ready_a()
        harness.set_irq_fault(IrqFault.SUPPRESSED, reason="MODEL_DROP_IRQ")
        _, transfer = begin_a(adapter, harness, 2, b"timeout")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(transfer)
        self.assertTrue(
            harness.mark_irq_timeout(transfer, reason="CALLER_TIMEOUT_THRESHOLD")
        )
        harness.set_irq_fault(IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertFalse(harness.mark_serializer_done(transfer))
        harness.inject_spurious_irq(reason="LATE_EDGE")
        self.assertEqual(harness.service_irq(), 0)
        self.assertEqual(adapter.observations, [])
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_UNCERTAIN")

    def test_stuck_and_spurious_irq_cannot_fabricate_completion(self) -> None:
        adapter, harness = ready_a()
        harness.set_irq_fault(IrqFault.STUCK_ASSERTED, reason="MODEL_STUCK_LINE")
        self.assertEqual(harness.service_irq(), 0)
        _, transfer = begin_a(adapter, harness, 3, b"real")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(transfer)
        self.assertEqual(harness.service_irq(), 1)
        self.assertTrue(harness.irq_asserted)
        self.assertEqual(harness.service_irq(), 0)
        self.assertEqual(len(adapter.observations), 2)
        harness.set_irq_fault(IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_unserviced_spurious_irq_prevents_model_pass(self) -> None:
        adapter, harness = ready_a()
        complete_a(adapter, harness, 2, b"valid-first")
        harness.inject_spurious_irq(reason="MODEL_SPURIOUS_EDGE")
        validation = harness.validate()
        self.assertEqual(validation.status, "A_SINGLE_QUAD_MODEL_NOT_READY")
        self.assertIn("IRQ_ASSERTED_WITHOUT_LATCH:A", validation.anomalies)
        self.assertEqual(harness.service_irq(), 0)
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_raw_bus_generation_change_quarantines_stale_latch(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 4, b"stale")
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(transfer, deliver_irq=False)
        harness.bus.reset()
        harness.bus.recover()
        self.assertFalse(harness.poll_completion(transfer))
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.observations, [])

    def test_device_reset_quarantines_pending_and_all_external_inflight(self) -> None:
        adapter, harness = ready_a()
        transfers = []
        attempts = []
        for logical_channel in (2, 3, 4):
            attempt, transfer = begin_a(
                adapter,
                harness,
                logical_channel,
                f"open-{logical_channel}".encode("ascii"),
            )
            attempts.append(attempt)
            transfers.append(transfer)
            if logical_channel != 4:
                harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        before = dict(adapter.resource_generation)
        harness.inject_device_reset(reason="MODEL_RESET_EDGE")
        self.assertEqual(adapter.bank_states[Bank.TTL], BankState.SAFE_DISABLED)
        self.assertEqual(adapter.bank_states[Bank.RS232], BankState.SAFE_DISABLED)
        for attempt in attempts:
            self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        for transfer in transfers:
            self.assertEqual(harness.transfer_states[transfer], TransferState.UNCERTAIN)
            self.assertFalse(harness.mark_serializer_done(transfer))
        for resource in ("BRIDGE_A_CH0", "BRIDGE_A_CH1", "BRIDGE_A_CH2"):
            self.assertEqual(adapter.resource_generation[resource], before[resource] + 1)
        self.assertEqual(adapter.resource_generation["UART1"], before["UART1"])
        self.assertEqual(adapter.resource_generation["UART2"], before["UART2"])
        self.assertEqual(harness.device_generation, 1)
        self.assertEqual(harness.routes.rx_reservation, "BRIDGE_A_CH3")
        self.assertEqual(adapter.observations, [])

    def test_reset_during_host_pending_releases_bus_and_rejects_callback(self) -> None:
        adapter, harness = ready_a()
        attempt, transfer = begin_a(adapter, harness, 2, b"pending-reset")
        self.assertEqual(harness.bus.active, transfer)
        harness.inject_device_reset(reason="MODEL_PENDING_RESET")
        self.assertIsNone(harness.bus.active)
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertFalse(
            harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )

    def test_recovery_requires_bridge_up_new_generation_config_idle_and_arm(self) -> None:
        adapter, harness = ready_a()
        harness.inject_device_reset(reason="MODEL_RESET_EDGE")
        harness.release_device_reset(reason="MODEL_RESET_RELEASE")
        adapter.enqueue(2, b"after-recovery")
        self.assertIsNone(adapter.begin_next(2))
        for resource in harness.routes.external_logical_resources:
            generation = adapter.resource_generation[resource]
            adapter.mark_resource_configured(resource, generation)
            adapter.mark_resource_idle(resource, generation)
        adapter.arm_outputs()
        attempt = adapter.begin_next(2)
        transfer = harness.start_host_write(
            attempt,
            chip_select_id="MODEL_CS_A",
        ).transfer
        harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        harness.mark_serializer_done(transfer)
        self.assertEqual(harness.service_irq(), 1)
        self.assertEqual(harness.validate().status, "A_SINGLE_QUAD_MODEL_PASS")

    def test_bus_reset_invalidates_external_resources_not_local_uarts(self) -> None:
        adapter, harness = ready_a()
        _, transfer = begin_a(adapter, harness, 4, b"bus-reset")
        before = dict(adapter.resource_generation)
        harness.inject_bus_reset(reason="MODEL_BUS_RESET")
        self.assertFalse(harness.bus.online)
        self.assertEqual(harness.bus.generation, 1)
        self.assertEqual(harness.device_generation, 1)
        for resource in harness.routes.external_logical_resources:
            self.assertEqual(adapter.resource_generation[resource], before[resource] + 1)
        self.assertEqual(adapter.resource_generation["UART1"], before["UART1"])
        self.assertEqual(adapter.resource_generation["UART2"], before["UART2"])
        self.assertFalse(
            harness.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        harness.release_bus_reset(reason="MODEL_BUS_RECOVERED")
        self.assertTrue(harness.bus.online)


class AClassEvidenceAndParityTests(unittest.TestCase):
    def test_a_and_d_deliver_same_mirrored_endpoint_manifest_in_model(self) -> None:
        a_adapter, a = ready_a(session_id="PARITY-A")
        d_adapter = ready_adapter("PARITY-D")
        d = DClassDualBridgeHarness(d_adapter)
        payloads = {2: b"p2", 3: b"p3", 4: b"p4"}

        for channel, payload in payloads.items():
            complete_a(a_adapter, a, channel, payload)
            d_adapter.enqueue(channel, payload)
            d_attempt = d_adapter.begin_next(channel)
            binding = d.projection.external_for_logical(channel)
            d_transfer = d.start_host_write(
                d_attempt,
                chip_select_id=d.bus.devices[binding.physical.device_id],
            ).transfer
            d.resolve_host_transfer(d_transfer, CommitResult.COMMITTED)
            d.mark_serializer_done(d_transfer)
            self.assertEqual(d.service_irq(d_transfer.device_id), 1)

        def manifest(adapter):
            return {
                (
                    item.endpoint,
                    item.logical_channel,
                    item.sequence,
                    item.payload,
                    item.test_crc32,
                )
                for item in adapter.observations
            }

        self.assertEqual(manifest(a_adapter), manifest(d_adapter))
        self.assertEqual(a.validate().status, "A_SINGLE_QUAD_MODEL_PASS")
        self.assertEqual(d.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_deterministic_digest_tracks_payload_session_and_rejection_identity(self) -> None:
        def completed_digest(payload: bytes, session_id: str) -> str:
            adapter, harness = ready_a(session_id=session_id)
            complete_a(adapter, harness, 2, payload)
            return harness.event_digest()

        self.assertEqual(
            completed_digest(b"same", "SESSION"),
            completed_digest(b"same", "SESSION"),
        )
        self.assertNotEqual(
            completed_digest(b"same", "SESSION"),
            completed_digest(b"different", "SESSION"),
        )
        self.assertNotEqual(
            completed_digest(b"same", "SESSION-A"),
            completed_digest(b"same", "SESSION-B"),
        )

        def rejected_digest(session_id: str) -> str:
            adapter, harness = ready_a(session_id=session_id)
            adapter.enqueue(2, b"wrong-cs")
            attempt = adapter.begin_next(2)
            with self.assertRaises(ValueError):
                harness.start_host_write(attempt, chip_select_id="WRONG")
            return harness.event_digest()

        self.assertNotEqual(
            rejected_digest("REJECT-A"),
            rejected_digest("REJECT-B"),
        )

    def test_empty_model_and_pass_never_claim_hardware_or_exact_part(self) -> None:
        adapter, harness = ready_a()
        validation = harness.validate()
        self.assertEqual(validation.status, "A_SINGLE_QUAD_MODEL_NOT_RUN")
        self.assertEqual(MODEL_CLASS, "ASSUMPTION / MODEL_ONLY")
        self.assertEqual(validation.hardware_status, HARDWARE_STATUS)
        self.assertEqual(HARDWARE_STATUS, "NOT_RUN / HARDWARE_REQUIRED")
        self.assertEqual(validation.exact_part_status, EXACT_PART_STATUS)
        self.assertEqual(EXACT_PART_STATUS, "UNKNOWN / NOT_SELECTED")
        self.assertEqual(
            validation.connector_delivery_status,
            "NOT_RUN / NOT_PROVEN",
        )
        self.assertEqual(RESET_POLICY, "GLOBAL_SAFE_DISABLE_BEFORE_RECONFIGURATION")


if __name__ == "__main__":
    unittest.main()
