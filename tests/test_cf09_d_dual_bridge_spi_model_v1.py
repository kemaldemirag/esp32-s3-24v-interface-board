"""Contract tests for the exact-part-neutral CF-09 class-D SPI fake."""

from __future__ import annotations

import unittest
from dataclasses import replace

from cf09_d_dual_bridge_spi_model_v1 import (
    CONNECTOR_DELIVERY_STATUS,
    EXACT_PART_STATUS,
    HARDWARE_STATUS,
    MODEL_CLASS,
    RESET_POLICY,
    RX_RESERVATION,
    DClassDualBridgeHarness,
    FakeSharedSpiBus,
    IrqFault,
    StartStatus,
    TransferState,
)
from cf09_host_model_v1 import Bank, BankState, CommitResult, Topology
from cf09_transport_adapter_v1 import (
    AttemptState,
    Cf09TransportAdapter,
    TxAttempt,
)


def ready_rig(
    *,
    session_id: str = "D-CLASS-TEST",
    chip_selects: dict[str, str] | None = None,
) -> tuple[Cf09TransportAdapter, DClassDualBridgeHarness]:
    adapter = Cf09TransportAdapter(
        Topology.mirrored(),
        session_id=session_id,
    )
    adapter.set_bridge_state("A", up=True, reason="MODEL_ONLY_AGGREGATE_READY")
    adapter.declare_all_resources_ready()
    adapter.arm_outputs()
    rig = DClassDualBridgeHarness(adapter, chip_selects=chip_selects)
    return adapter, rig


def begin_external(
    adapter: Cf09TransportAdapter,
    rig: DClassDualBridgeHarness,
    logical_channel: int,
    payload: bytes,
):
    adapter.enqueue(logical_channel, payload)
    attempt = adapter.begin_next(logical_channel)
    if attempt is None:
        raise AssertionError("external attempt unexpectedly unavailable")
    binding = rig.projection.external_for_logical(logical_channel)
    if binding.physical is None:
        raise AssertionError("external binding unexpectedly lacks a physical target")
    ack = rig.start_host_write(
        attempt,
        chip_select_id=rig.bus.devices[binding.physical.device_id],
    )
    if ack.status != StartStatus.STARTED or ack.transfer is None:
        raise AssertionError("fake SPI transfer unexpectedly failed to start")
    return attempt, ack.transfer


def complete_external(
    adapter: Cf09TransportAdapter,
    rig: DClassDualBridgeHarness,
    logical_channel: int,
    payload: bytes,
):
    attempt, transfer = begin_external(adapter, rig, logical_channel, payload)
    if not rig.resolve_host_transfer(transfer, CommitResult.COMMITTED):
        raise AssertionError("fake host commit unexpectedly failed")
    if not rig.mark_serializer_done(transfer):
        raise AssertionError("fake serializer completion unexpectedly failed")
    if rig.service_irq(transfer.device_id) != 1:
        raise AssertionError("fake IRQ did not complete exactly one transfer")
    return attempt, transfer


class DClassProjectionTests(unittest.TestCase):
    def test_d_bindings_match_two_dual_allocation_and_rx_is_not_tx(self) -> None:
        adapter, rig = ready_rig()
        bindings = rig.projection.bindings
        self.assertEqual(
            [bindings[index].scenario_resource for index in range(5)],
            [
                "UART1",
                "UART2",
                "BRIDGE_A_CH0",
                "BRIDGE_A_CH1",
                "BRIDGE_B_CH0",
            ],
        )
        self.assertEqual(
            [bindings[index].logical_resource for index in range(5)],
            [
                "UART1",
                "UART2",
                "BRIDGE_A_CH0",
                "BRIDGE_A_CH1",
                "BRIDGE_A_CH2",
            ],
        )
        self.assertEqual(bindings[4].physical.device_id, "B")
        self.assertEqual(bindings[4].physical.channel, 0)
        self.assertEqual(rig.projection.rx_reservation, RX_RESERVATION)
        with self.assertRaisesRegex(ValueError, "reserved for RX"):
            rig.projection.logical_for_physical_tx("B", 1)
        self.assertEqual(adapter.routes[4].resource, "BRIDGE_A_CH2")

    def test_local_channel_and_wrong_chip_select_fail_closed(self) -> None:
        adapter, rig = ready_rig()
        adapter.enqueue(0, b"local")
        local_attempt = adapter.begin_next(0)
        with self.assertRaisesRegex(ValueError, "local"):
            rig.start_host_write(local_attempt, chip_select_id="MODEL_CS_A")
        self.assertIsNone(rig.bus.active)

        adapter.enqueue(2, b"external")
        external_attempt = adapter.begin_next(2)
        with self.assertRaisesRegex(ValueError, "chip-select"):
            rig.start_host_write(
                external_attempt,
                chip_select_id="MODEL_CS_B",
            )
        self.assertIsNone(rig.bus.active)
        self.assertEqual(
            adapter.attempt_states[external_attempt.token],
            AttemptState.COMMIT_PENDING,
        )

    def test_shared_bus_has_one_owner_and_unique_symbolic_chip_selects(self) -> None:
        _, rig = ready_rig()
        self.assertEqual(rig.bus.initialization_count, 1)
        self.assertEqual(
            rig.bus.devices,
            {"A": "MODEL_CS_A", "B": "MODEL_CS_B"},
        )
        with self.assertRaisesRegex(RuntimeError, "already initialized"):
            rig.bus.initialize("SECOND_OWNER")

        bus = FakeSharedSpiBus()
        bus.initialize("OWNER")
        bus.attach_device("OWNER", "A", "CS0")
        with self.assertRaisesRegex(ValueError, "duplicate chip-select"):
            bus.attach_device("OWNER", "B", "CS0")
        with self.assertRaisesRegex(ValueError, "duplicate SPI device"):
            bus.attach_device("OWNER", "A", "CS1")


class DClassHostCommitTests(unittest.TestCase):
    def test_malformed_callback_device_is_rejected_without_mutation(self) -> None:
        adapter, rig = ready_rig()
        attempt, transfer = begin_external(adapter, rig, 2, b"callback-boundary")
        malformed = replace(transfer, device_id="C")
        self.assertFalse(
            rig.resolve_host_transfer(malformed, CommitResult.COMMITTED)
        )
        self.assertEqual(rig.bus.active, transfer)
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.COMMIT_PENDING,
        )
        self.assertEqual(rig.events[-1].kind, "LATE_HOST_CALLBACK")
        self.assertIn("unknown_device", rig.events[-1].detail)
        self.assertTrue(
            rig.resolve_host_transfer(transfer, CommitResult.NOT_COMMITTED)
        )

    def test_forged_attempt_payload_or_endpoints_are_rejected(self) -> None:
        adapter, rig = ready_rig()
        adapter.enqueue(2, b"original")
        attempt = adapter.begin_next(2)

        forged_payload = TxAttempt(
            attempt.token,
            b"forged",
            attempt.endpoints,
        )
        with self.assertRaisesRegex(ValueError, "payload_mismatch"):
            rig.start_host_write(
                forged_payload,
                chip_select_id="MODEL_CS_A",
            )
        forged_endpoints = TxAttempt(
            attempt.token,
            attempt.payload,
            ("TTL2",),
        )
        with self.assertRaisesRegex(ValueError, "endpoint_manifest_mismatch"):
            rig.start_host_write(
                forged_endpoints,
                chip_select_id="MODEL_CS_A",
            )
        self.assertIsNone(rig.bus.active)
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.COMMIT_PENDING,
        )

    def test_shared_bus_serializes_device_a_and_b_chip_selects(self) -> None:
        adapter, rig = ready_rig()
        adapter.enqueue(2, b"a")
        adapter.enqueue(4, b"b")
        attempt_a = adapter.begin_next(2)
        attempt_b = adapter.begin_next(4)
        started_a = rig.start_host_write(
            attempt_a,
            chip_select_id="MODEL_CS_A",
        )
        busy_b = rig.start_host_write(
            attempt_b,
            chip_select_id="MODEL_CS_B",
        )
        self.assertEqual(started_a.status, StartStatus.STARTED)
        self.assertEqual(busy_b, type(busy_b)(StartStatus.BUS_BUSY, None))
        self.assertEqual(rig.bus.active.device_id, "A")

        rig.resolve_host_transfer(started_a.transfer, CommitResult.NOT_COMMITTED)
        started_b = rig.start_host_write(
            attempt_b,
            chip_select_id="MODEL_CS_B",
        )
        self.assertEqual(started_b.status, StartStatus.STARTED)
        self.assertEqual(started_b.transfer.transfer_no, 2)
        self.assertEqual(rig.bus.active.device_id, "B")
        rig.resolve_host_transfer(started_b.transfer, CommitResult.NOT_COMMITTED)
        self.assertIsNone(rig.bus.active)

    def test_host_commit_is_not_serializer_or_connector_completion(self) -> None:
        adapter, rig = ready_rig()
        attempt, transfer = begin_external(adapter, rig, 2, b"separate-phases")
        self.assertTrue(
            rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.INFLIGHT,
        )
        self.assertEqual(adapter.observations, [])
        self.assertEqual(
            rig.transfer_states[transfer],
            TransferState.HOST_COMMITTED,
        )

        self.assertTrue(rig.mark_serializer_done(transfer))
        self.assertEqual(adapter.observations, [])
        self.assertEqual(
            rig.transfer_states[transfer],
            TransferState.SERIALIZER_DONE_LATCHED,
        )
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertEqual(
            {item.endpoint for item in adapter.observations},
            {"TTL2", "RS2322"},
        )
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")
        self.assertEqual(rig.validate().connector_delivery_status, CONNECTOR_DELIVERY_STATUS)

    def test_devices_can_finish_out_of_order_without_cross_binding(self) -> None:
        adapter, rig = ready_rig()
        _, transfer_a = begin_external(adapter, rig, 2, b"payload-a")
        rig.resolve_host_transfer(transfer_a, CommitResult.COMMITTED)
        _, transfer_b = begin_external(adapter, rig, 4, b"payload-b")
        rig.resolve_host_transfer(transfer_b, CommitResult.COMMITTED)

        rig.mark_serializer_done(transfer_a)
        rig.mark_serializer_done(transfer_b)
        self.assertEqual(rig.service_irq("B"), 1)
        self.assertEqual(
            {item.endpoint for item in adapter.observations},
            {"TTL4", "RS2324"},
        )
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertEqual(
            [item.device_id for item in rig.completed_transfers],
            ["B", "A"],
        )
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_not_committed_retry_uses_fresh_parent_and_spi_tokens(self) -> None:
        adapter, rig = ready_rig()
        first_attempt, first_transfer = begin_external(adapter, rig, 2, b"retry")
        self.assertTrue(
            rig.resolve_host_transfer(first_transfer, CommitResult.NOT_COMMITTED)
        )
        self.assertEqual(adapter.pending_count(), 1)

        second_attempt = adapter.begin_next(2)
        self.assertGreater(
            second_attempt.token.attempt_no,
            first_attempt.token.attempt_no,
        )
        second = rig.start_host_write(
            second_attempt,
            chip_select_id="MODEL_CS_A",
        ).transfer
        self.assertNotEqual(first_transfer, second)
        self.assertFalse(
            rig.resolve_host_transfer(first_transfer, CommitResult.COMMITTED)
        )
        self.assertTrue(rig.resolve_host_transfer(second, CommitResult.COMMITTED))
        rig.mark_serializer_done(second)
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertEqual(len(adapter.observations), 2)
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_unknown_host_commit_is_quarantined_and_not_auto_retried(self) -> None:
        adapter, rig = ready_rig()
        attempt, transfer = begin_external(adapter, rig, 4, b"ambiguous")
        self.assertTrue(rig.resolve_host_transfer(transfer, CommitResult.UNKNOWN))
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.UNCERTAIN,
        )
        self.assertEqual(adapter.pending_count(), 0)
        self.assertIsNone(adapter.begin_next(4))
        self.assertFalse(rig.mark_serializer_done(transfer))
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_UNCERTAIN")


class DClassIrqAndResetTests(unittest.TestCase):
    def test_wrong_source_and_duplicate_completion_cannot_duplicate_delivery(self) -> None:
        adapter, rig = ready_rig()
        _, transfer = begin_external(adapter, rig, 2, b"irq-binding")
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        self.assertFalse(
            rig.mark_serializer_done(transfer, source_device="B")
        )
        self.assertFalse(
            rig.mark_serializer_done(transfer, source_channel=1)
        )
        self.assertEqual(rig.service_irq("B"), 0)
        self.assertTrue(rig.mark_serializer_done(transfer))
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertFalse(rig.mark_serializer_done(transfer))
        self.assertEqual(rig.service_irq("A"), 0)
        self.assertEqual(len(adapter.observations), 2)

    def test_suppressed_irq_can_use_explicit_status_poll_fallback(self) -> None:
        adapter, rig = ready_rig()
        rig.set_irq_fault("B", IrqFault.SUPPRESSED, reason="MODEL_DROP_IRQ")
        _, transfer = begin_external(adapter, rig, 4, b"poll-fallback")
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        rig.mark_serializer_done(transfer)
        self.assertFalse(rig.irq_asserted["B"])
        self.assertEqual(rig.service_irq("B"), 0)
        self.assertTrue(rig.poll_completion(transfer))
        rig.set_irq_fault("B", IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_lost_irq_timeout_is_uncertain_and_late_irq_is_ignored(self) -> None:
        adapter, rig = ready_rig()
        rig.set_irq_fault("A", IrqFault.SUPPRESSED, reason="MODEL_DROP_IRQ")
        _, transfer = begin_external(adapter, rig, 2, b"timeout")
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        rig.mark_serializer_done(transfer)
        self.assertTrue(
            rig.mark_irq_timeout(transfer, reason="CALLER_TIMEOUT_THRESHOLD")
        )
        rig.set_irq_fault("A", IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertFalse(rig.mark_serializer_done(transfer))
        rig.inject_spurious_irq("A", reason="LATE_EDGE")
        self.assertEqual(rig.service_irq("A"), 0)
        self.assertEqual(adapter.observations, [])
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_UNCERTAIN")

    def test_stuck_and_spurious_irq_cannot_fabricate_completion(self) -> None:
        adapter, rig = ready_rig()
        rig.set_irq_fault("A", IrqFault.STUCK_ASSERTED, reason="MODEL_STUCK_LINE")
        self.assertEqual(rig.service_irq("A"), 0)
        _, transfer = begin_external(adapter, rig, 3, b"real")
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        rig.mark_serializer_done(transfer)
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertTrue(rig.irq_asserted["A"])
        self.assertEqual(rig.service_irq("A"), 0)
        self.assertEqual(len(adapter.observations), 2)
        rig.set_irq_fault("A", IrqFault.NORMAL, reason="MODEL_FAULT_CLEARED")
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_unserviced_spurious_irq_prevents_model_pass(self) -> None:
        adapter, rig = ready_rig()
        complete_external(adapter, rig, 2, b"valid-first")
        rig.inject_spurious_irq("B", reason="MODEL_SPURIOUS_EDGE")
        validation = rig.validate()
        self.assertEqual(validation.status, "D_DUAL_BRIDGE_MODEL_NOT_READY")
        self.assertIn("IRQ_ASSERTED_WITHOUT_LATCH:B", validation.anomalies)
        self.assertEqual(rig.service_irq("B"), 0)
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_stale_latch_after_raw_bus_generation_change_is_quarantined(self) -> None:
        adapter, rig = ready_rig()
        attempt, transfer = begin_external(adapter, rig, 4, b"stale-generation")
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        rig.mark_serializer_done(transfer, deliver_irq=False)

        rig.bus.reset()
        rig.bus.recover()
        self.assertFalse(rig.poll_completion(transfer))
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.UNCERTAIN,
        )
        self.assertEqual(adapter.observations, [])
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_UNCERTAIN")

    def test_device_reset_uses_global_safe_disable_and_rejects_late_callbacks(self) -> None:
        adapter, rig = ready_rig()
        attempt_a, transfer_a = begin_external(adapter, rig, 2, b"a-open")
        rig.resolve_host_transfer(transfer_a, CommitResult.COMMITTED)
        attempt_b, transfer_b = begin_external(adapter, rig, 4, b"b-open")
        rig.resolve_host_transfer(transfer_b, CommitResult.COMMITTED)
        before = dict(adapter.resource_generation)

        rig.inject_device_reset("A", reason="MODEL_RESET_EDGE")
        self.assertEqual(RESET_POLICY, "GLOBAL_SAFE_DISABLE_BEFORE_RECONFIGURATION")
        self.assertEqual(adapter.bank_states[Bank.TTL], BankState.SAFE_DISABLED)
        self.assertEqual(adapter.bank_states[Bank.RS232], BankState.SAFE_DISABLED)
        self.assertEqual(adapter.attempt_states[attempt_a.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.attempt_states[attempt_b.token], AttemptState.UNCERTAIN)
        self.assertEqual(rig.transfer_states[transfer_a], TransferState.UNCERTAIN)
        self.assertEqual(rig.transfer_states[transfer_b], TransferState.UNCERTAIN)
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH0"],
            before["BRIDGE_A_CH0"] + 1,
        )
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH1"],
            before["BRIDGE_A_CH1"] + 1,
        )
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH2"],
            before["BRIDGE_A_CH2"],
        )
        self.assertFalse(rig.mark_serializer_done(transfer_a))
        self.assertFalse(rig.mark_serializer_done(transfer_b))
        self.assertEqual(adapter.observations, [])

    def test_device_recovery_requires_new_generation_config_and_idle(self) -> None:
        adapter, rig = ready_rig()
        rig.inject_device_reset("A", reason="MODEL_RESET_EDGE")
        rig.release_device_reset("A", reason="MODEL_RESET_RELEASE")
        adapter.enqueue(2, b"after-recovery")
        self.assertIsNone(adapter.begin_next(2))

        for resource in ("BRIDGE_A_CH0", "BRIDGE_A_CH1"):
            generation = adapter.resource_generation[resource]
            adapter.mark_resource_configured(resource, generation)
            adapter.mark_resource_idle(resource, generation)
        adapter.arm_outputs()

        attempt = adapter.begin_next(2)
        transfer = rig.start_host_write(
            attempt,
            chip_select_id="MODEL_CS_A",
        ).transfer
        rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        rig.mark_serializer_done(transfer)
        self.assertEqual(rig.service_irq("A"), 1)
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_PASS")

    def test_shared_bus_reset_invalidates_external_not_local_resources(self) -> None:
        adapter, rig = ready_rig()
        _, transfer = begin_external(adapter, rig, 4, b"bus-reset")
        before = dict(adapter.resource_generation)
        rig.inject_bus_reset(reason="MODEL_BUS_CONTROLLER_RESET")
        self.assertFalse(rig.bus.online)
        self.assertEqual(rig.bus.generation, 1)
        self.assertEqual(rig.device_generation, {"A": 1, "B": 1})
        self.assertEqual(adapter.resource_generation["UART1"], before["UART1"])
        self.assertEqual(adapter.resource_generation["UART2"], before["UART2"])
        for resource in ("BRIDGE_A_CH0", "BRIDGE_A_CH1", "BRIDGE_A_CH2"):
            self.assertEqual(
                adapter.resource_generation[resource],
                before[resource] + 1,
            )
        self.assertFalse(
            rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        rig.release_bus_reset(reason="MODEL_BUS_CONTROLLER_RECOVERED")
        self.assertTrue(rig.bus.online)

    def test_device_b_reset_invalidates_b0_projection_and_preserves_rx_reservation(self) -> None:
        adapter, rig = ready_rig()
        before = dict(adapter.resource_generation)
        rig.inject_device_reset("B", reason="MODEL_B_RESET")
        self.assertEqual(rig.device_generation, {"A": 0, "B": 1})
        self.assertEqual(rig.projection.rx_reservation, "BRIDGE_B_CH1")
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH2"],
            before["BRIDGE_A_CH2"] + 1,
        )
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH0"],
            before["BRIDGE_A_CH0"],
        )
        self.assertEqual(
            adapter.resource_generation["BRIDGE_A_CH1"],
            before["BRIDGE_A_CH1"],
        )

    def test_reset_during_host_pending_quarantines_and_releases_bus(self) -> None:
        adapter, rig = ready_rig()
        attempt, transfer = begin_external(adapter, rig, 2, b"pending-reset")
        self.assertEqual(rig.bus.active, transfer)
        rig.inject_device_reset("A", reason="MODEL_PENDING_RESET")
        self.assertIsNone(rig.bus.active)
        self.assertEqual(
            adapter.attempt_states[attempt.token],
            AttemptState.UNCERTAIN,
        )
        self.assertFalse(
            rig.resolve_host_transfer(transfer, CommitResult.COMMITTED)
        )
        self.assertEqual(adapter.observations, [])


class DClassEvidenceBoundaryTests(unittest.TestCase):
    def test_identical_runs_have_identical_trace_and_payload_changes_it(self) -> None:
        def digest(payload: bytes, session_id: str = "DIGEST-SESSION") -> str:
            adapter, rig = ready_rig(session_id=session_id)
            complete_external(adapter, rig, 2, payload)
            return rig.event_digest()

        self.assertEqual(digest(b"same"), digest(b"same"))
        self.assertNotEqual(digest(b"same"), digest(b"different"))
        self.assertNotEqual(
            digest(b"same", "DIGEST-SESSION-A"),
            digest(b"same", "DIGEST-SESSION-B"),
        )

    def test_pretransfer_rejection_digest_keeps_parent_session_identity(self) -> None:
        def rejected_digest(session_id: str) -> str:
            adapter, rig = ready_rig(session_id=session_id)
            adapter.enqueue(2, b"wrong-cs")
            attempt = adapter.begin_next(2)
            with self.assertRaises(ValueError):
                rig.start_host_write(
                    attempt,
                    chip_select_id="MODEL_CS_B",
                )
            return rig.event_digest()

        self.assertNotEqual(
            rejected_digest("REJECT-SESSION-A"),
            rejected_digest("REJECT-SESSION-B"),
        )

    def test_model_metadata_never_claims_exact_part_or_hardware_pass(self) -> None:
        adapter, rig = ready_rig()
        self.assertEqual(rig.validate().status, "D_DUAL_BRIDGE_MODEL_NOT_RUN")
        self.assertEqual(MODEL_CLASS, "ASSUMPTION / MODEL_ONLY")
        self.assertEqual(rig.validate().hardware_status, HARDWARE_STATUS)
        self.assertEqual(HARDWARE_STATUS, "NOT_RUN / HARDWARE_REQUIRED")
        self.assertEqual(rig.validate().exact_part_status, EXACT_PART_STATUS)
        self.assertEqual(EXACT_PART_STATUS, "UNKNOWN / NOT_SELECTED")
        self.assertEqual(
            rig.validate().connector_delivery_status,
            "NOT_RUN / NOT_PROVEN",
        )


if __name__ == "__main__":
    unittest.main()
