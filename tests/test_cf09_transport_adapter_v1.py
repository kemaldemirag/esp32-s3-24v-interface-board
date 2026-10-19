"""Contract tests for the token-safe CF-09 transport integration seam."""

from __future__ import annotations

import unittest

from cf09_host_model_v1 import Bank, BankState, CommitResult, Frame, Topology
from cf09_transport_adapter_v1 import (
    AttemptState,
    Cf09TransportAdapter,
    EnqueueStatus,
    FrameKey,
    StaleHandle,
    StaleResolution,
    U32_MAX,
)


def ready_adapter(
    override_pairs: frozenset[int] = frozenset(),
    *,
    session_id: str = "TEST-SESSION",
    queue_limit: int | None = None,
    epoch: int = 1,
) -> Cf09TransportAdapter:
    adapter = Cf09TransportAdapter(
        Topology(override_pairs),
        session_id=session_id,
        queue_limit=queue_limit,
        epoch=epoch,
    )
    for bridge in sorted(
        {route.bridge for route in adapter.routes.values() if route.bridge is not None}
    ):
        adapter.set_bridge_state(bridge, up=True, reason="MODEL_ONLY_DISCOVERY")
    adapter.declare_all_resources_ready()
    adapter.arm_outputs()
    return adapter


class CF09TransportAdapterTests(unittest.TestCase):
    def test_late_completion_cannot_complete_new_frame_on_same_serializer(self) -> None:
        adapter = ready_adapter()
        first_ack = adapter.enqueue(0, b"old")
        first = adapter.begin_next(0)
        self.assertIsNotNone(first)
        self.assertTrue(adapter.resolve_commit(first.token, CommitResult.COMMITTED))

        adapter.force_safe_disabled("MODEL_ONLY_RESET")
        self.assertEqual(adapter.attempt_states[first.token], AttemptState.UNCERTAIN)
        adapter.arm_outputs()

        second_ack = adapter.enqueue(0, b"new")
        second = adapter.begin_next(0)
        self.assertIsNotNone(second)
        self.assertTrue(adapter.resolve_commit(second.token, CommitResult.COMMITTED))
        self.assertEqual(first_ack.frame.sequence, 0)
        self.assertEqual(second_ack.frame.sequence, 1)

        self.assertFalse(
            adapter.resolve_complete(first.token, evidence="STALE_ISR_CALLBACK")
        )
        self.assertEqual(adapter.observations, [])
        self.assertEqual(adapter.attempt_states[second.token], AttemptState.INFLIGHT)

        self.assertTrue(
            adapter.resolve_complete(second.token, evidence="CURRENT_ISR_CALLBACK")
        )
        self.assertEqual(
            {(item.sequence, item.endpoint) for item in adapter.observations},
            {(1, "TTL0"), (1, "RS2320")},
        )
        self.assertTrue(
            any(event.kind == "LATE_COMPLETION_EVENT" for event in adapter.events)
        )

    def test_stale_commit_after_bridge_reset_cannot_resolve_new_attempt(self) -> None:
        adapter = ready_adapter(frozenset(range(5)))
        adapter.enqueue(2, b"old-pending")
        old = adapter.begin_next(2)
        self.assertIsNotNone(old)

        adapter.set_bridge_state("A", up=False, reason="MODEL_ONLY_RESET")
        self.assertEqual(adapter.attempt_states[old.token], AttemptState.UNCERTAIN)
        old_generation = old.token.resource_generation
        self.assertGreater(
            adapter.resource_generation[old.token.resource],
            old_generation,
        )

        adapter.set_bridge_state("A", up=True, reason="MODEL_ONLY_RECOVERY")
        adapter.force_safe_disabled("MODEL_ONLY_RECOVERY_CONFIG")
        for resource in sorted(
            route.resource for route in adapter.routes.values() if route.bridge == "A"
        ):
            generation = adapter.resource_generation[resource]
            adapter.mark_resource_configured(resource, generation)
            adapter.mark_resource_idle(resource, generation)
        adapter.arm_outputs()

        adapter.enqueue(2, b"new-pending")
        new = adapter.begin_next(2)
        self.assertIsNotNone(new)
        self.assertNotEqual(old.token, new.token)
        self.assertFalse(adapter.resolve_commit(old.token, CommitResult.COMMITTED))
        self.assertEqual(adapter.attempt_states[new.token], AttemptState.COMMIT_PENDING)
        self.assertTrue(adapter.resolve_commit(new.token, CommitResult.COMMITTED))

    def test_not_committed_retains_frame_but_requires_fresh_attempt_token(self) -> None:
        adapter = ready_adapter()
        ack = adapter.enqueue(0, b"retryable")
        first = adapter.begin_next(0)
        self.assertTrue(adapter.resolve_commit(first.token, CommitResult.NOT_COMMITTED))
        self.assertEqual(adapter.pending_count(), 1)
        self.assertEqual(adapter.attempt_states[first.token], AttemptState.NOT_COMMITTED)

        second = adapter.begin_next(0)
        self.assertEqual(second.token.frame, ack.frame)
        self.assertEqual(second.token.attempt_no, first.token.attempt_no + 1)
        self.assertFalse(adapter.resolve_commit(first.token, CommitResult.COMMITTED))
        self.assertTrue(adapter.resolve_commit(second.token, CommitResult.COMMITTED))
        self.assertTrue(adapter.resolve_complete(second.token, evidence="MODEL_ISR"))
        self.assertEqual(adapter.pending_count(), 0)

    def test_unknown_commit_is_uncertain_and_never_auto_retried(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"unknown")
        attempt = adapter.begin_next(0)
        self.assertTrue(adapter.resolve_commit(attempt.token, CommitResult.UNKNOWN))
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.pending_count(), 0)
        self.assertIsNone(adapter.begin_next())
        self.assertEqual(
            adapter.uncertain_ranges(),
            [{"epoch": 1, "logical_channel": 0, "start": 0, "end": 0}],
        )

    def test_duplicate_and_conflicting_callbacks_do_not_duplicate_delivery(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"once")
        attempt = adapter.begin_next(0)
        self.assertTrue(adapter.resolve_commit(attempt.token, CommitResult.COMMITTED))
        self.assertFalse(adapter.resolve_commit(attempt.token, CommitResult.UNKNOWN))
        self.assertTrue(adapter.resolve_complete(attempt.token, evidence="MODEL_ISR"))
        self.assertFalse(adapter.resolve_complete(attempt.token, evidence="DUPLICATE_ISR"))
        self.assertEqual(len(adapter.observations), 2)
        self.assertEqual(adapter.observation_anomalies(), [])

    def test_readiness_is_explicit_generation_bound_and_all_or_nothing(self) -> None:
        adapter = Cf09TransportAdapter(
            Topology.independent(),
            session_id="TEST-READINESS",
        )
        with self.assertRaises(RuntimeError):
            adapter.arm_outputs()
        self.assertEqual(
            set(adapter.bank_states.values()),
            {BankState.SAFE_DISABLED},
        )

        with self.assertRaises(RuntimeError):
            adapter.declare_all_resources_ready()
        adapter.set_bridge_state("A", up=True, reason="MODEL_ONLY_DISCOVERY")
        adapter.set_bridge_state("B", up=True, reason="MODEL_ONLY_DISCOVERY")

        for resource in sorted(adapter.required_resources):
            adapter.mark_resource_configured(resource, 0)
        with self.assertRaises(RuntimeError):
            adapter.arm_outputs()
        self.assertEqual(
            set(adapter.bank_states.values()),
            {BankState.SAFE_DISABLED},
        )

        for resource in sorted(adapter.required_resources):
            adapter.mark_resource_idle(resource, 0)
        adapter.arm_outputs()
        self.assertEqual(set(adapter.bank_states.values()), {BankState.ENABLED})

        adapter.set_bridge_state("B", up=False, reason="MODEL_ONLY_RESET")
        adapter.force_safe_disabled("MODEL_ONLY_REARM")
        adapter.set_bridge_state("B", up=True, reason="MODEL_ONLY_RECOVERY")
        with self.assertRaises(RuntimeError):
            adapter.arm_outputs()
        b_resource = next(
            route.resource for route in adapter.routes.values() if route.bridge == "B"
        )
        with self.assertRaises(ValueError):
            adapter.mark_resource_configured(
                b_resource,
                adapter.resource_generation[b_resource] - 1,
            )

    def test_restart_is_disable_first_discards_queue_and_rejects_old_tokens(self) -> None:
        adapter = ready_adapter(frozenset(range(5)))

        adapter.enqueue(2, b"inflight")
        inflight = adapter.begin_next(2)
        adapter.resolve_commit(inflight.token, CommitResult.COMMITTED)

        adapter.enqueue(6, b"commit-pending")
        pending = adapter.begin_next(6)
        adapter.enqueue(0, b"queued")

        start_index = len(adapter.events)
        report = adapter.restart(2, reason="MODEL_ONLY_WATCHDOG")
        restart_events = adapter.events[start_index:]

        self.assertEqual(report.uncertain_attempts, 2)
        self.assertEqual(report.stale_queued_frames, 1)
        self.assertEqual(
            report.stale_queued,
            (
                StaleHandle(
                    session_id="TEST-SESSION",
                    frame=FrameKey(epoch=1, logical_channel=0, sequence=0),
                ),
            ),
        )
        self.assertEqual(restart_events[0].kind, "BANK_STATE")
        self.assertEqual(restart_events[1].kind, "BANK_STATE")
        first_uncertain = next(
            index for index, event in enumerate(restart_events) if event.kind == "UNCERTAIN"
        )
        self.assertGreater(first_uncertain, 1)
        self.assertEqual(restart_events[-1].kind, "RESTART")
        self.assertFalse(adapter.resolve_complete(inflight.token, evidence="LATE_ISR"))
        self.assertFalse(adapter.resolve_commit(pending.token, CommitResult.COMMITTED))

        adapter.declare_all_resources_ready()
        adapter.arm_outputs()
        ack = adapter.enqueue(2, b"new-epoch")
        self.assertEqual((ack.frame.epoch, ack.frame.sequence), (2, 0))

    def test_bridge_a_fault_does_not_cancel_bridge_b_attempt(self) -> None:
        adapter = ready_adapter(frozenset(range(5)))
        adapter.enqueue(2, b"bridge-a")
        bridge_a = adapter.begin_next(2)
        adapter.resolve_commit(bridge_a.token, CommitResult.COMMITTED)

        adapter.enqueue(6, b"bridge-b")
        bridge_b = adapter.begin_next(6)
        adapter.resolve_commit(bridge_b.token, CommitResult.COMMITTED)

        adapter.set_bridge_state("A", up=False, reason="MODEL_ONLY_ISOLATION")
        self.assertEqual(adapter.attempt_states[bridge_a.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.attempt_states[bridge_b.token], AttemptState.INFLIGHT)
        self.assertTrue(adapter.resolve_complete(bridge_b.token, evidence="MODEL_ISR"))
        self.assertEqual(
            {item.endpoint for item in adapter.observations},
            {"RS2321"},
        )

    def test_direct_bank_disable_invalidates_affected_attempt_before_rearm(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"stale")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)

        adapter.disable_bank(Bank.TTL, reason="MODEL_ONLY_LOCAL_DISABLE")
        adapter.disable_bank(Bank.RS232, reason="MODEL_ONLY_LOCAL_DISABLE")
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        adapter.arm_outputs()
        self.assertFalse(adapter.resolve_complete(attempt.token, evidence="LATE_ISR"))
        self.assertEqual(adapter.observations, [])

    def test_mirrored_endpoint_fault_stops_pair_without_silent_single_leg(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"pair")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        adapter.inject_endpoint_fault("RS2320")

        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertFalse(adapter.resolve_complete(attempt.token, evidence="LATE_ISR"))
        self.assertEqual(adapter.observations, [])
        adapter.clear_endpoint_fault("RS2320")
        self.assertIsNone(adapter.begin_next())

    def test_backpressure_is_explicit_and_does_not_consume_sequence(self) -> None:
        adapter = ready_adapter(queue_limit=1)
        first = adapter.enqueue(0, b"first")
        rejected = adapter.enqueue(0, b"rejected")
        self.assertEqual(first.status, EnqueueStatus.ENQUEUED)
        self.assertEqual(rejected.status, EnqueueStatus.BACKPRESSURE)
        self.assertIsNone(rejected.frame)

        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        adapter.resolve_complete(attempt.token, evidence="MODEL_ISR")
        second = adapter.enqueue(0, b"second")
        self.assertEqual(second.frame.sequence, 1)

    def test_completion_requires_nonempty_evidence_label(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"evidence")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        with self.assertRaises(ValueError):
            adapter.resolve_complete(attempt.token, evidence="   ")
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.INFLIGHT)

    def test_invalid_commit_outcome_does_not_mutate_pending_attempt(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"invalid-outcome")
        attempt = adapter.begin_next(0)
        with self.assertRaises(ValueError):
            adapter.resolve_commit(attempt.token, "INVALID")
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.COMMIT_PENDING)
        self.assertEqual(adapter.pending_count(), 1)
        self.assertEqual(adapter.pending_by_serializer[attempt.token.serializer], attempt.token)

    def test_local_resource_invalidation_quarantines_open_attempt(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"uart-reset")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        adapter.invalidate_resource("UART1", reason="MODEL_ONLY_UART_RESET")

        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertFalse(adapter.resolve_complete(attempt.token, evidence="LATE_ISR"))
        self.assertNotIn("UART1", adapter.configured_generation)
        self.assertNotIn("UART1", adapter.idle_generation)

    def test_reconfigure_requires_explicit_invalidation_and_new_idle_check(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"open")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)

        with self.assertRaises(RuntimeError):
            adapter.mark_resource_configured("UART1", 0)
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.INFLIGHT)

        adapter.invalidate_resource("UART1", reason="MODEL_ONLY_RECONFIGURE")
        generation = adapter.resource_generation["UART1"]
        adapter.force_safe_disabled("MODEL_ONLY_REARM")
        adapter.mark_resource_configured("UART1", generation)
        self.assertNotIn("UART1", adapter.idle_generation)
        with self.assertRaises(RuntimeError):
            adapter.arm_outputs()
        adapter.mark_resource_idle("UART1", generation)
        adapter.arm_outputs()

    def test_validation_never_passes_open_or_uncertain_work(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"queued")
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_INCOMPLETE")

        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        inflight = adapter.validate()
        self.assertEqual(inflight.status, "TX_ADAPTER_MODEL_INCOMPLETE")
        self.assertTrue(any(item.startswith("MISSING_COMPLETION:") for item in inflight.anomalies))
        self.assertTrue(any(item.startswith("MISSING_EXPECTED:") for item in inflight.anomalies))

        adapter.mark_uncertain(attempt.token, "completion_timeout")
        uncertain = adapter.validate()
        self.assertEqual(uncertain.status, "TX_ADAPTER_MODEL_UNCERTAIN")
        self.assertGreater(uncertain.uncertain, 0)

    def test_cold_boot_session_identity_rejects_old_instance_callback(self) -> None:
        old_adapter = ready_adapter(session_id="BOOT-A")
        new_adapter = ready_adapter(session_id="BOOT-B")
        for adapter, payload in ((old_adapter, b"old"), (new_adapter, b"new")):
            adapter.enqueue(0, payload)

        old_attempt = old_adapter.begin_next(0)
        new_attempt = new_adapter.begin_next(0)
        old_adapter.resolve_commit(old_attempt.token, CommitResult.COMMITTED)
        new_adapter.resolve_commit(new_attempt.token, CommitResult.COMMITTED)

        self.assertNotEqual(old_attempt.token, new_attempt.token)
        self.assertFalse(
            new_adapter.resolve_complete(old_attempt.token, evidence="OLD_BOOT_ISR")
        )
        self.assertEqual(new_adapter.observations, [])
        self.assertTrue(
            new_adapter.resolve_complete(new_attempt.token, evidence="CURRENT_BOOT_ISR")
        )

    def test_session_identity_is_mandatory(self) -> None:
        with self.assertRaises(ValueError):
            Cf09TransportAdapter(Topology.mirrored(), session_id="   ")

    def test_epoch_and_sequence_must_fit_crc_envelope_u32_fields(self) -> None:
        with self.assertRaises(ValueError):
            Cf09TransportAdapter(
                Topology.mirrored(),
                session_id="TEST-EPOCH-OVERFLOW",
                epoch=U32_MAX + 1,
            )
        adapter = ready_adapter(session_id="TEST-U32-BOUNDARY")
        with self.assertRaises(ValueError):
            adapter.restart(U32_MAX + 1, reason="MODEL_ONLY_OVERFLOW")
        self.assertEqual(adapter.epoch, 1)

        terminal_epoch = ready_adapter(
            session_id="TEST-EPOCH-EXHAUSTED",
            epoch=U32_MAX,
        )
        self.assertEqual(
            terminal_epoch.validate().status,
            "TX_ADAPTER_MODEL_EXHAUSTED",
        )
        self.assertTrue(
            any(
                item.startswith("EPOCH_SPACE_EXHAUSTED:")
                for item in terminal_epoch.validate().anomalies
            )
        )

        boundary_adapter = ready_adapter(session_id="TEST-U32-LAST-VALUE")
        boundary_adapter.next_sequence[0] = U32_MAX
        boundary = boundary_adapter.enqueue(0, b"last-u32-sequence")
        self.assertEqual(boundary.frame.sequence, U32_MAX)

        adapter.next_sequence[0] = U32_MAX + 1
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_EXHAUSTED")
        with self.assertRaises(OverflowError):
            adapter.enqueue(0, b"must-fail-closed")
        self.assertEqual(adapter.pending_count(), 0)
        self.assertEqual(adapter.events[-1].kind, "SEQUENCE_SPACE_EXHAUSTED")
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_EXHAUSTED")

        adapter.restart(2, reason="MODEL_ONLY_NEW_EPOCH")
        adapter.declare_all_resources_ready()
        adapter.arm_outputs()
        self.assertEqual(adapter.next_sequence[0], 0)
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")

    def test_public_api_cannot_bypass_arm_preflight(self) -> None:
        adapter = Cf09TransportAdapter(
            Topology.mirrored(),
            session_id="TEST-NO-ARM-BYPASS",
        )
        self.assertFalse(hasattr(adapter, "transition_bank"))
        with self.assertRaises(RuntimeError):
            adapter.arm_outputs()
        self.assertEqual(set(adapter.bank_states.values()), {BankState.SAFE_DISABLED})

    def test_reentrant_reset_is_rejected_without_partial_completion(self) -> None:
        adapter = ready_adapter(session_id="TEST-REENTRANT")
        adapter.enqueue(0, b"atomic")
        attempt = adapter.begin_next(0)
        adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
        original_is_ready = adapter._is_resource_ready

        def reentrant_reset(resource: str) -> bool:
            adapter.force_safe_disabled("REENTRANT_RESET")
            return original_is_ready(resource)

        adapter._is_resource_ready = reentrant_reset
        try:
            with self.assertRaisesRegex(RuntimeError, "concurrent/reentrant mutation"):
                adapter.resolve_complete(attempt.token, evidence="MODEL_ISR")
        finally:
            adapter._is_resource_ready = original_is_ready

        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.INFLIGHT)
        self.assertEqual(
            adapter.inflight_by_serializer[attempt.token.serializer],
            attempt.token,
        )
        self.assertEqual(adapter.observations, [])
        self.assertTrue(adapter.resolve_complete(attempt.token, evidence="MODEL_ISR_RETRY"))

    def test_restart_stale_requeue_requires_explicit_reconciliation(self) -> None:
        adapter = ready_adapter(
            session_id="TEST-STALE-REQUEUE",
            queue_limit=1,
        )
        old = adapter.enqueue(0, b"preserve-payload")
        report = adapter.restart(2, reason="MODEL_ONLY_RESTART")
        handle = report.stale_queued[0]
        adapter.declare_all_resources_ready()
        adapter.arm_outputs()

        self.assertEqual(handle.frame, old.frame)
        self.assertEqual(
            adapter.validate().status,
            "TX_ADAPTER_MODEL_RECONCILIATION_REQUIRED",
        )

        filler = adapter.enqueue(0, b"current-epoch")
        deferred = adapter.reconcile_stale(
            handle,
            resolution=StaleResolution.REENQUEUED_AS_NEW_FRAME,
            reason="CALLER_REQUESTED_RETRY",
        )
        self.assertEqual(deferred.status, EnqueueStatus.BACKPRESSURE)
        self.assertIn(handle, adapter.unreconciled_stale)

        filler_attempt = adapter.begin_next(0)
        adapter.resolve_commit(filler_attempt.token, CommitResult.COMMITTED)
        adapter.resolve_complete(filler_attempt.token, evidence="MODEL_ISR")
        replacement = adapter.reconcile_stale(
            handle,
            resolution=StaleResolution.REENQUEUED_AS_NEW_FRAME,
            reason="CALLER_REQUESTED_RETRY",
        )
        self.assertEqual(replacement.status, EnqueueStatus.ENQUEUED)
        self.assertEqual(replacement.frame.epoch, 2)
        self.assertEqual(replacement.frame.sequence, filler.frame.sequence + 1)
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_INCOMPLETE")

        replacement_attempt = adapter.begin_next(0)
        self.assertEqual(replacement_attempt.payload, b"preserve-payload")
        adapter.resolve_commit(replacement_attempt.token, CommitResult.COMMITTED)
        adapter.resolve_complete(replacement_attempt.token, evidence="MODEL_ISR")
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")
        with self.assertRaises(KeyError):
            adapter.reconcile_stale(
                handle,
                resolution=StaleResolution.CANCELLED_BY_CALLER,
                reason="DUPLICATE_DECISION",
            )

    def test_restart_stale_cancel_is_explicit_and_auditable(self) -> None:
        adapter = ready_adapter(session_id="TEST-STALE-CANCEL")
        old = adapter.enqueue(0, b"cancel-me")
        report = adapter.restart(2, reason="MODEL_ONLY_RESTART")
        handle = report.stale_queued[0]
        adapter.declare_all_resources_ready()
        adapter.arm_outputs()
        adapter.reconcile_stale(
            handle,
            resolution=StaleResolution.CANCELLED_BY_CALLER,
            reason="CALLER_CONFIRMED_CANCEL",
        )

        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")
        self.assertIn("CANCELLED_BY_CALLER", adapter.stale_resolution_ledger[handle])
        self.assertTrue(
            any(event.kind == "STALE_QUEUE_CANCELLED" for event in adapter.events)
        )

    def test_foreign_session_cannot_reconcile_same_frame_key(self) -> None:
        first = ready_adapter(session_id="BOOT-A")
        second = ready_adapter(session_id="BOOT-B")
        first_ack = first.enqueue(0, b"payload-a")
        second_ack = second.enqueue(0, b"payload-b")
        first_handle = first.restart(2, reason="MODEL_ONLY_RESTART").stale_queued[0]
        second_handle = second.restart(2, reason="MODEL_ONLY_RESTART").stale_queued[0]

        self.assertEqual(first_ack.frame, second_ack.frame)
        self.assertNotEqual(first_handle, second_handle)
        with self.assertRaisesRegex(ValueError, "another session"):
            second.reconcile_stale(
                first_handle,
                resolution=StaleResolution.CANCELLED_BY_CALLER,
                reason="FOREIGN_DECISION",
            )
        self.assertIn(second_handle, second.unreconciled_stale)
        self.assertEqual(second.unreconciled_stale[second_handle].payload, b"payload-b")
        self.assertTrue(
            any(
                event.kind == "FOREIGN_STALE_RECONCILIATION"
                for event in second.events
            )
        )

    def test_validation_includes_operational_health(self) -> None:
        fresh = Cf09TransportAdapter(
            Topology.mirrored(),
            session_id="TEST-HEALTH-FRESH",
        )
        self.assertEqual(fresh.validate().status, "TX_ADAPTER_MODEL_NOT_READY")
        self.assertTrue(
            any(item.startswith("RESOURCE_NOT_READY:") for item in fresh.validate().anomalies)
        )

        faulted = ready_adapter(session_id="TEST-HEALTH-FAULT")
        faulted.inject_endpoint_fault("RS2320")
        self.assertEqual(faulted.validate().status, "TX_ADAPTER_MODEL_FAULTED")
        faulted.clear_endpoint_fault("RS2320")
        self.assertEqual(faulted.validate().status, "TX_ADAPTER_MODEL_PASS")

        bridge_down = ready_adapter(
            frozenset(range(5)),
            session_id="TEST-HEALTH-BRIDGE",
        )
        bridge_down.set_bridge_state("A", up=False, reason="MODEL_ONLY_FAULT")
        self.assertEqual(bridge_down.validate().status, "TX_ADAPTER_MODEL_FAULTED")

        disabled = ready_adapter(session_id="TEST-HEALTH-DISABLED")
        disabled.force_safe_disabled("MODEL_ONLY_SAFE_STATE")
        self.assertEqual(disabled.validate().status, "TX_ADAPTER_MODEL_NOT_READY")

    def test_fault_clear_rejects_unknown_endpoint_and_logs_known_noop(self) -> None:
        adapter = ready_adapter(session_id="TEST-FAULT-CLEAR")
        with self.assertRaises(ValueError):
            adapter.clear_endpoint_fault("NOT_A_REAL_ENDPOINT")
        adapter.clear_endpoint_fault("RS2320")
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")
        self.assertEqual(adapter.events[-1].kind, "ENDPOINT_FAULT_CLEAR_NOOP")

    def test_resource_reconfiguration_requires_disable_first(self) -> None:
        adapter = ready_adapter(session_id="TEST-DISABLE-FIRST")
        with self.assertRaisesRegex(RuntimeError, "safe-disable"):
            adapter.mark_resource_configured("UART1", 0)

        adapter.force_safe_disabled("MODEL_ONLY_RECONFIGURE")
        adapter.mark_resource_configured("UART1", 0)
        adapter.mark_resource_idle("UART1", 0)
        adapter.arm_outputs()
        self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")

    def test_all_32_subsets_match_independent_literal_route_table(self) -> None:
        descriptors = {
            0: ("S0", "UART1", None, None),
            1: ("S1", "UART2", None, None),
            2: ("S2", "BRIDGE_A_CH0", "A", 0),
            3: ("S3", "BRIDGE_A_CH1", "A", 1),
            4: ("S4", "BRIDGE_A_CH2", "A", 2),
            5: ("S5", "BRIDGE_A_CH3", "A", 3),
            6: ("S6", "BRIDGE_B_CH0", "B", 0),
            7: ("S7", "BRIDGE_B_CH1", "B", 1),
            8: ("S8", "BRIDGE_B_CH2", "B", 2),
            9: ("S9", "BRIDGE_B_CH3", "B", 3),
        }
        for mask in range(32):
            overrides = frozenset(index for index in range(5) if mask & (1 << index))
            adapter = Cf09TransportAdapter(
                Topology(overrides),
                session_id=f"TEST-ROUTES-{mask}",
            )
            with self.subTest(overrides=sorted(overrides)):
                self.assertEqual(set(adapter.routes), set(range(5)) | {5 + i for i in overrides})
                for logical_channel, route in adapter.routes.items():
                    pair = logical_channel if logical_channel < 5 else logical_channel - 5
                    if logical_channel < 5:
                        endpoints = (f"TTL{pair}",)
                        if pair not in overrides:
                            endpoints += (f"RS232{pair}",)
                    else:
                        endpoints = (f"RS232{pair}",)
                    self.assertEqual(
                        (
                            route.serializer,
                            route.resource,
                            route.bridge,
                            route.bridge_channel,
                        ),
                        descriptors[logical_channel],
                    )
                    self.assertEqual(route.endpoints, endpoints)

    def test_all_32_subsets_preserve_mapping_and_conservation(self) -> None:
        expected_endpoints = {f"TTL{i}" for i in range(5)} | {f"RS232{i}" for i in range(5)}
        for mask in range(32):
            override_pairs = frozenset(index for index in range(5) if mask & (1 << index))
            with self.subTest(override_pairs=sorted(override_pairs)):
                adapter = ready_adapter(override_pairs)
                for channel in adapter.topology.logical_channels:
                    adapter.enqueue(channel, f"L{channel}".encode("ascii"))
                while adapter.pending_count():
                    attempt = adapter.begin_next()
                    self.assertIsNotNone(attempt)
                    route = adapter.routes[attempt.token.frame.logical_channel]
                    self.assertEqual(attempt.token.serializer, route.serializer)
                    self.assertEqual(attempt.token.resource, route.resource)
                    self.assertEqual(
                        attempt.token.resource_generation,
                        adapter.resource_generation[route.resource],
                    )
                    adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
                    adapter.resolve_complete(attempt.token, evidence="MODEL_ISR")

                self.assertEqual(
                    {item.endpoint for item in adapter.observations},
                    expected_endpoints,
                )
                self.assertEqual(adapter.observation_anomalies(), [])
                self.assertEqual(len(adapter.completed_tokens), 5 + len(override_pairs))
                self.assertEqual(adapter.validate().status, "TX_ADAPTER_MODEL_PASS")

    def test_timeout_marks_attempt_uncertain_without_replay(self) -> None:
        adapter = ready_adapter()
        adapter.enqueue(0, b"timeout")
        attempt = adapter.begin_next(0)
        self.assertTrue(adapter.mark_uncertain(attempt.token, "commit_timeout"))
        self.assertEqual(adapter.attempt_states[attempt.token], AttemptState.UNCERTAIN)
        self.assertEqual(adapter.pending_count(), 0)
        self.assertIsNone(adapter.begin_next())

    def test_identical_runs_have_identical_event_digest(self) -> None:
        digests: list[str] = []
        for _ in range(2):
            adapter = ready_adapter(frozenset({0, 2, 4}))
            for channel in adapter.topology.logical_channels:
                adapter.enqueue(channel, f"deterministic-{channel}".encode("ascii"))
            while adapter.pending_count():
                attempt = adapter.begin_next()
                adapter.resolve_commit(attempt.token, CommitResult.COMMITTED)
                adapter.resolve_complete(attempt.token, evidence="MODEL_ISR")
            digests.append(adapter.event_digest())
        self.assertEqual(digests[0], digests[1])

    def test_event_digest_distinguishes_payload_content(self) -> None:
        digests: list[str] = []
        for payload in (b"A", b"A", b"B"):
            adapter = ready_adapter(session_id="TEST-DIGEST-PAYLOAD")
            adapter.enqueue(0, payload)
            digests.append(adapter.event_digest())
        self.assertEqual(digests[0], digests[1])
        self.assertNotEqual(digests[0], digests[2])

    def test_event_digest_distinguishes_real_crc32_collision_pair(self) -> None:
        payloads = (bytes.fromhex("A2BCB4A97D"), bytes.fromhex("1E1B4B7EBF"))
        frames = [Frame.create(1, 0, 0, payload) for payload in payloads]
        self.assertEqual(frames[0].test_crc32, 0x9A8571F2)
        self.assertEqual(frames[0].test_crc32, frames[1].test_crc32)

        digests: list[str] = []
        for payload in payloads:
            adapter = ready_adapter(session_id="TEST-DIGEST-CRC-COLLISION")
            adapter.enqueue(0, payload)
            digests.append(adapter.event_digest())
        self.assertNotEqual(digests[0], digests[1])

        rejected_digests: list[str] = []
        for payload in payloads:
            adapter = ready_adapter(
                session_id="TEST-DIGEST-BACKPRESSURE",
                queue_limit=1,
            )
            adapter.enqueue(0, b"queue-filler")
            self.assertEqual(
                adapter.enqueue(0, payload).status,
                EnqueueStatus.BACKPRESSURE,
            )
            rejected_digests.append(adapter.event_digest())
        self.assertNotEqual(rejected_digests[0], rejected_digests[1])


if __name__ == "__main__":
    unittest.main()
