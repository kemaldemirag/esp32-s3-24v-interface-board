"""Deterministic tests for the CF-09 MODEL_ONLY host prototype."""

from __future__ import annotations

import unittest
from dataclasses import replace

from cf09_host_model_v1 import (
    Bank,
    BankState,
    CommitResult,
    HostModel,
    ReceiverOracle,
    RxDecision,
    Topology,
    build_routes,
    run_demo,
    shared_rx_decision,
)


def armed_model(
    override_pairs: frozenset[int] = frozenset(),
    *,
    queue_limit: int | None = None,
) -> HostModel:
    model = HostModel(Topology(override_pairs), queue_limit=queue_limit)
    model.declare_resources_ready()
    model.arm_outputs()
    return model


class CF09HostModelTests(unittest.TestCase):
    def test_routes_cover_all_32_hybrid_subsets(self) -> None:
        expected_endpoints = {f"TTL{i}" for i in range(5)} | {f"RS232{i}" for i in range(5)}
        for mask in range(32):
            override_pairs = frozenset(index for index in range(5) if mask & (1 << index))
            with self.subTest(override_pairs=sorted(override_pairs)):
                topology = Topology(override_pairs)
                routes = build_routes(topology)
                expected_channels = set(range(5)) | {5 + index for index in override_pairs}
                expected_serializers = {f"S{index}" for index in range(5)} | {
                    f"S{5 + index}" for index in override_pairs
                }
                self.assertEqual(set(routes), expected_channels)
                self.assertEqual(
                    {endpoint for route in routes.values() for endpoint in route.endpoints},
                    expected_endpoints,
                )
                self.assertEqual(
                    {route.serializer for route in routes.values()},
                    expected_serializers,
                )
                uses_bridge_b = any(route.bridge == "B" for route in routes.values())
                self.assertEqual(uses_bridge_b, any(index >= 1 for index in override_pairs))

        mirrored = build_routes(Topology.mirrored())
        self.assertEqual(mirrored[0].endpoints, ("TTL0", "RS2320"))
        self.assertEqual(mirrored[4].serializer, "S4")
        independent = build_routes(Topology.independent())
        self.assertEqual(independent[0].endpoints, ("TTL0",))
        self.assertEqual(independent[5].endpoints, ("RS2320",))
        self.assertEqual(independent[9].serializer, "S9")

        only_pair_four = build_routes(Topology(frozenset({4})))
        self.assertEqual(set(only_pair_four), {0, 1, 2, 3, 4, 9})
        self.assertEqual(only_pair_four[9].endpoints, ("RS2324",))
        self.assertEqual(only_pair_four[9].bridge, "B")

    def test_mirrored_uses_one_completion_for_two_observations(self) -> None:
        model = armed_model()
        frame = model.enqueue(0, b"mirror")
        self.assertIsNotNone(frame)
        self.assertTrue(model.service_one())

        accepted = [event for event in model.ledger if event.kind == "ACCEPTED_BY_RESOURCE"]
        completed = [event for event in model.ledger if event.kind == "MODEL_SERIALIZER_COMPLETE"]
        self.assertEqual(len(accepted), 1)
        self.assertEqual(len(completed), 1)
        self.assertEqual(completed[0].endpoints, ("TTL0", "RS2320"))
        self.assertEqual(len(model.observations), 2)
        self.assertEqual(model.observations[0].payload, model.observations[1].payload)
        self.assertEqual(model.observations[0].test_crc32, model.observations[1].test_crc32)
        self.assertEqual(
            ReceiverOracle(Topology.mirrored()).validate(
                model.observations,
                model.expected_observations(),
            ),
            [],
        )

    def test_independent_ten_channels_are_isolated(self) -> None:
        model = armed_model(frozenset(range(5)))
        for channel in range(10):
            model.enqueue(channel, f"channel-{channel}".encode("ascii"))
        self.assertEqual(model.run_until_stalled(), 10)
        self.assertEqual(len(model.observations), 10)

        by_endpoint = {observation.endpoint: observation for observation in model.observations}
        for index in range(5):
            self.assertEqual(by_endpoint[f"TTL{index}"].logical_channel, index)
            self.assertEqual(by_endpoint[f"RS232{index}"].logical_channel, 5 + index)
        self.assertEqual(
            ReceiverOracle(Topology.independent()).validate(
                model.observations,
                model.expected_observations(),
            ),
            [],
        )

    def test_round_robin_preserves_per_channel_fifo(self) -> None:
        model = armed_model()
        model.enqueue(0, b"zero-a")
        model.enqueue(0, b"zero-b")
        model.enqueue(1, b"one-a")
        model.run_until_stalled()

        accepted_channels = [
            event.logical_channel
            for event in model.ledger
            if event.kind == "ACCEPTED_BY_RESOURCE"
        ]
        self.assertEqual(accepted_channels, [0, 1, 0])
        ttl0_sequences = [
            observation.sequence for observation in model.observations if observation.endpoint == "TTL0"
        ]
        self.assertEqual(ttl0_sequences, [0, 1])

    def test_oracle_exposes_crc_missing_duplicate_and_reorder(self) -> None:
        model = armed_model()
        model.enqueue(0, b"first")
        model.enqueue(0, b"second")
        model.run_until_stalled()
        ttl = [observation for observation in model.observations if observation.endpoint == "TTL0"]
        expected_ttl = [
            observation
            for observation in model.expected_observations()
            if observation.endpoint == "TTL0"
        ]
        self.assertEqual(len(ttl), 2)

        corrupt = replace(ttl[0], payload=b"bit-flip", test_crc32=ttl[0].test_crc32)
        anomalies = ReceiverOracle(Topology.mirrored()).validate([corrupt], expected_ttl)
        self.assertTrue(any(item.startswith("CRC_MISMATCH:") for item in anomalies))

        anomalies = ReceiverOracle(Topology.mirrored()).validate(
            [ttl[0], ttl[0], ttl[1]],
            expected_ttl,
        )
        self.assertTrue(any(item.startswith("DUPLICATE:") for item in anomalies))

        anomalies = ReceiverOracle(Topology.mirrored()).validate(
            [ttl[1], ttl[0]],
            expected_ttl,
        )
        self.assertTrue(any(item.startswith("MISSING:") for item in anomalies))
        self.assertTrue(any(item.startswith("REORDER:") for item in anomalies))

    def test_oracle_detects_terminal_empty_and_whole_leg_loss(self) -> None:
        model = armed_model()
        model.enqueue(0, b"first")
        model.enqueue(0, b"last")
        model.run_until_stalled()
        expected = model.expected_observations()
        oracle = ReceiverOracle(Topology.mirrored())

        self.assertTrue(
            any(item.startswith("MISSING_EXPECTED:") for item in oracle.validate([], expected))
        )

        without_terminal = [
            observation
            for observation in model.observations
            if not (observation.endpoint == "TTL0" and observation.sequence == 1)
        ]
        self.assertTrue(
            any(
                item.startswith("MISSING_EXPECTED:TTL0")
                for item in oracle.validate(without_terminal, expected)
            )
        )

        without_rs232_leg = [
            observation for observation in model.observations if observation.endpoint != "RS2320"
        ]
        self.assertTrue(
            any(
                item.startswith("MISSING_EXPECTED:RS2320")
                for item in oracle.validate(without_rs232_leg, expected)
            )
        )

    def test_shared_rx_policy_never_silently_remaps(self) -> None:
        self.assertEqual(
            shared_rx_decision(
                Topology.mirrored(),
                assume_shared_rx=True,
                rx_config_token="opaque-a",
                s5_tx_config_token=None,
            ),
            RxDecision.FREE_BRIDGE_A_CHANNEL,
        )
        self.assertEqual(
            shared_rx_decision(
                Topology.independent(),
                assume_shared_rx=True,
                rx_config_token="opaque-a",
                s5_tx_config_token="opaque-a",
            ),
            RxDecision.SHARED_RX_ALLOWED,
        )
        for assume_shared, rx_token, tx_token in (
            (True, "opaque-a", "opaque-b"),
            (False, "opaque-a", "opaque-a"),
        ):
            with self.subTest(assume_shared=assume_shared, rx=rx_token, tx=tx_token):
                self.assertEqual(
                    shared_rx_decision(
                        Topology.independent(),
                        assume_shared_rx=assume_shared,
                        rx_config_token=rx_token,
                        s5_tx_config_token=tx_token,
                    ),
                    RxDecision.NEEDS_DEDICATED_RX,
                )

        for mask in range(1, 31):
            override_pairs = frozenset(index for index in range(5) if mask & (1 << index))
            expected = (
                RxDecision.NEEDS_DEDICATED_RX
                if 0 in override_pairs
                else RxDecision.FREE_BRIDGE_A_CHANNEL
            )
            with self.subTest(rx_subset=sorted(override_pairs)):
                self.assertEqual(
                    shared_rx_decision(
                        Topology(override_pairs),
                        assume_shared_rx=False,
                        rx_config_token="opaque-a",
                        s5_tx_config_token="opaque-a",
                    ),
                    expected,
                )

    def test_bridge_a_preaccept_fault_does_not_stall_local_or_bridge_b(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.set_bridge("A", up=False, reason="MODEL_ONLY_PRE_ACCEPT")
        model.enqueue(2, b"bridge-a")
        model.enqueue(6, b"bridge-b")
        model.enqueue(0, b"local")
        model.run_until_stalled()

        endpoints = {observation.endpoint for observation in model.observations}
        self.assertEqual(endpoints, {"TTL0", "RS2321"})
        self.assertEqual(len(model.queues[2]), 1)

        model.set_bridge("A", up=True, reason="MODEL_ONLY_RECOVERY")
        model.declare_resources_ready(
            route.resource for route in model.routes.values() if route.bridge == "A"
        )
        model.run_until_stalled()
        self.assertIn("TTL2", {observation.endpoint for observation in model.observations})

    def test_postaccept_bridge_reset_is_uncertain_and_not_retried(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(2, b"accepted-not-complete")
        self.assertTrue(model.service_one(2, serializer_complete=False))
        self.assertIn("S2", model.inflight)

        model.set_bridge("A", up=False, reason="MODEL_ONLY_POST_ACCEPT_RESET")
        self.assertNotIn("S2", model.inflight)
        self.assertEqual(model.pending_count(), 0)
        self.assertEqual(len(model.observations), 0)
        self.assertEqual(
            model.uncertain_ranges(),
            [{"epoch": 1, "logical_channel": 2, "start": 0, "end": 0}],
        )

        model.set_bridge("A", up=True, reason="MODEL_ONLY_RECOVERY")
        self.assertEqual(model.run_until_stalled(), 0)
        self.assertEqual(len(model.observations), 0)

    def test_postaccept_bridge_b_reset_is_uncertain_and_not_retried(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(6, b"bridge-b-inflight")
        self.assertTrue(model.service_one(6, serializer_complete=False))
        self.assertIn("S6", model.inflight)

        model.set_bridge("B", up=False, reason="MODEL_ONLY_POST_ACCEPT_RESET")
        self.assertNotIn("S6", model.inflight)
        self.assertEqual(model.observations, [])
        self.assertEqual(
            model.uncertain_ranges(),
            [{"epoch": 1, "logical_channel": 6, "start": 0, "end": 0}],
        )

    def test_postaccept_safe_disable_prevents_false_completion(self) -> None:
        model = armed_model()
        model.enqueue(0, b"disable-after-accept")
        self.assertTrue(model.service_one(0, serializer_complete=False))
        model.force_safe_disabled("MODEL_ONLY_INTERLOCK")

        self.assertFalse(model.complete_serializer("S0"))
        self.assertEqual(model.observations, [])
        self.assertEqual(len(model.uncertain), 1)
        disable_ticks = [event.tick for event in model.ledger if event.kind == "BANK_STATE"][-2:]
        uncertain_tick = [event.tick for event in model.ledger if event.kind == "UNCERTAIN"][-1]
        self.assertLess(max(disable_ticks), uncertain_tick)

    def test_direct_bank_disable_cannot_complete_stale_inflight_after_rearm(self) -> None:
        model = armed_model()
        model.enqueue(0, b"direct-disable-after-accept")
        self.assertTrue(model.service_one(0, serializer_complete=False))

        model.transition_bank(Bank.TTL, BankState.SAFE_DISABLED)
        model.transition_bank(Bank.RS232, BankState.SAFE_DISABLED)
        self.assertEqual(len(model.uncertain), 1)
        self.assertEqual(model.observations, [])

        model.arm_outputs()
        self.assertFalse(model.complete_serializer("S0"))
        self.assertEqual(model.observations, [])

    def test_postaccept_endpoint_fault_prevents_false_completion(self) -> None:
        model = armed_model()
        model.enqueue(0, b"fault-after-accept")
        self.assertTrue(model.service_one(0, serializer_complete=False))
        model.inject_endpoint_fault("RS2320")

        self.assertFalse(model.complete_serializer("S0"))
        self.assertEqual(model.observations, [])
        self.assertEqual(len(model.uncertain), 1)
        anomalies = ReceiverOracle(Topology.mirrored()).validate(
            model.observations,
            model.expected_observations(),
        )
        self.assertTrue(any(item.startswith("MISSING_EXPECTED:") for item in anomalies))

    def test_unknown_transaction_has_no_automatic_retry(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(6, b"unknown")
        self.assertTrue(model.service_one(6, commit_result=CommitResult.UNKNOWN))
        self.assertEqual(model.pending_count(), 0)
        self.assertEqual(len(model.observations), 0)
        self.assertEqual(len(model.uncertain), 1)
        self.assertEqual(model.run_until_stalled(), 0)

    def test_not_committed_transaction_stays_queued_for_safe_retry(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(6, b"not-committed")
        self.assertTrue(model.service_one(6, commit_result=CommitResult.NOT_COMMITTED))
        self.assertEqual(model.pending_count(), 1)
        self.assertEqual(len(model.observations), 0)
        self.assertEqual(len(model.uncertain), 0)

        self.assertTrue(model.service_one(6, commit_result=CommitResult.COMMITTED))
        self.assertEqual(model.pending_count(), 0)
        self.assertEqual(len(model.observations), 1)
        self.assertEqual(model.observations[0].sequence, 0)

    def test_restart_discards_stale_queue_and_uses_new_epoch(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(2, b"old-inflight")
        model.enqueue(2, b"old-queued")
        model.service_one(2, serializer_complete=False)
        model.restart(2, reason="MODEL_ONLY_WATCHDOG")

        self.assertEqual(model.pending_count(), 0)
        self.assertEqual(len(model.uncertain), 1)
        self.assertTrue(any(event.kind == "STALE_QUEUE_DISCARDED" for event in model.ledger))
        self.assertFalse(model.complete_serializer("S2"))

        model.declare_resources_ready()
        model.arm_outputs()
        new_frame = model.enqueue(2, b"new-epoch")
        self.assertIsNotNone(new_frame)
        self.assertEqual((new_frame.epoch, new_frame.sequence), (2, 0))
        model.run_until_stalled()
        self.assertEqual({observation.epoch for observation in model.observations}, {2})

    def test_backpressure_is_explicit_and_not_a_silent_drop(self) -> None:
        model = HostModel(Topology.mirrored(), queue_limit=2)
        self.assertIsNotNone(model.enqueue(0, b"one"))
        self.assertIsNotNone(model.enqueue(0, b"two"))
        self.assertIsNone(model.enqueue(0, b"three"))
        self.assertEqual(len(model.queues[0]), 2)
        backpressure = [event for event in model.ledger if event.kind == "BACKPRESSURE"]
        self.assertEqual(len(backpressure), 1)
        self.assertIn("silent_drop=false", backpressure[0].detail)

    def test_mirrored_leg_fault_stops_the_pair(self) -> None:
        model = armed_model()
        model.inject_endpoint_fault("RS2320")
        model.enqueue(0, b"pair")
        self.assertFalse(model.service_one())
        self.assertEqual(model.pending_count(), 1)
        self.assertEqual(model.observations, [])
        degraded = [event for event in model.ledger if event.kind == "PAIR_DEGRADED"]
        self.assertEqual(len(degraded), 1)

        model.clear_endpoint_fault("RS2320")
        self.assertEqual(model.run_until_stalled(), 1)
        self.assertEqual(
            {observation.endpoint for observation in model.observations},
            {"TTL0", "RS2320"},
        )

    def test_safe_disabled_blocks_all_egress(self) -> None:
        model = HostModel(Topology.mirrored())
        model.enqueue(0, b"held-during-boot")
        self.assertEqual(model.run_until_stalled(), 0)
        self.assertEqual(model.observations, [])
        self.assertEqual(model.pending_count(), 1)

        with self.assertRaises(RuntimeError):
            model.arm_outputs()
        model.declare_resources_ready()
        model.arm_outputs()
        self.assertEqual(model.run_until_stalled(), 1)
        self.assertEqual(len(model.observations), 2)

    def test_bridge_must_be_up_and_redeclared_ready_before_arm(self) -> None:
        model = HostModel(Topology.independent())
        model.set_bridge("A", up=False, reason="MODEL_ONLY_BOOT")
        with self.assertRaises(RuntimeError):
            model.declare_resources_ready()

        non_a_resources = {
            route.resource for route in model.routes.values() if route.bridge != "A"
        }
        model.declare_resources_ready(non_a_resources)
        with self.assertRaises(RuntimeError):
            model.arm_outputs()

        model.set_bridge("A", up=True, reason="MODEL_ONLY_BOOT_RECOVERY")
        model.declare_resources_ready()
        model.arm_outputs()
        self.assertEqual(model.bank_states[Bank.TTL], BankState.ENABLED)

    def test_illegal_output_enable_transition_is_rejected(self) -> None:
        model = HostModel(Topology.mirrored())
        with self.assertRaises(ValueError):
            model.transition_bank(Bank.TTL, BankState.ENABLED)

    def test_arm_preflight_does_not_partially_enable_banks(self) -> None:
        model = HostModel(Topology.mirrored())
        model.declare_resources_ready()
        model.transition_bank(Bank.RS232, BankState.CONFIGURING)
        with self.assertRaises(ValueError):
            model.arm_outputs()
        self.assertEqual(model.bank_states[Bank.TTL], BankState.SAFE_DISABLED)
        self.assertEqual(model.bank_states[Bank.RS232], BankState.CONFIGURING)

    def test_accepted_and_model_serializer_complete_are_distinct(self) -> None:
        model = armed_model(frozenset(range(5)))
        model.enqueue(6, b"deferred")
        model.service_one(6, serializer_complete=False)
        kinds = [event.kind for event in model.ledger]
        self.assertIn("ACCEPTED_BY_RESOURCE", kinds)
        self.assertNotIn("MODEL_SERIALIZER_COMPLETE", kinds)

        self.assertTrue(model.complete_serializer("S6"))
        self.assertEqual(
            len([event for event in model.ledger if event.kind == "MODEL_SERIALIZER_COMPLETE"]),
            1,
        )

    def test_identical_input_has_identical_ledger_digest(self) -> None:
        subset = frozenset({0, 2, 4})
        first = armed_model(subset)
        second = armed_model(subset)
        for model in (first, second):
            for channel in model.topology.logical_channels:
                model.enqueue(channel, f"deterministic-{channel}".encode("ascii"))
            model.run_until_stalled()
        self.assertEqual(first.ledger_digest(), second.ledger_digest())

    def test_demo_covers_mirrored_and_independent_bounds(self) -> None:
        mirrored = run_demo(frozenset(), 2)
        independent = run_demo(frozenset(range(5)), 2)
        self.assertEqual(mirrored["status"], "TX_HOST_MODEL_PASS")
        self.assertEqual(independent["status"], "TX_HOST_MODEL_PASS")
        self.assertEqual(mirrored["logical_channel_count"], 5)
        self.assertEqual(independent["logical_channel_count"], 10)
        self.assertEqual(mirrored["logical_channel_ids"], [0, 1, 2, 3, 4])
        self.assertEqual(independent["logical_channel_ids"], list(range(10)))
        self.assertEqual(mirrored["model_endpoint_observations"], 20)
        self.assertEqual(independent["model_endpoint_observations"], 20)
        self.assertEqual(mirrored["rx_decision"], "FREE_BRIDGE_A_CHANNEL")
        self.assertEqual(independent["rx_decision"], "NOT_EVALUATED")

        dedicated = run_demo(
            frozenset({0}),
            1,
            assume_shared_rx=False,
        )
        shared = run_demo(
            frozenset({0}),
            1,
            assume_shared_rx=True,
            rx_config_token="opaque-a",
            s5_tx_config_token="opaque-a",
        )
        self.assertEqual(dedicated["rx_decision"], "NEEDS_DEDICATED_RX")
        self.assertEqual(shared["rx_decision"], "SHARED_RX_ALLOWED")

    def test_demo_runs_all_32_hybrid_subsets_with_sparse_channel_ids(self) -> None:
        for mask in range(32):
            override_pairs = frozenset(index for index in range(5) if mask & (1 << index))
            with self.subTest(override_pairs=sorted(override_pairs)):
                result = run_demo(override_pairs, 1)
                self.assertEqual(result["status"], "TX_HOST_MODEL_PASS")
                self.assertEqual(result["model_endpoint_observations"], 10)
                self.assertEqual(result["pending_frames"], 0)
                self.assertEqual(result["uncertain_ranges"], [])

    def test_uncertain_ranges_coalesce_and_keep_gaps(self) -> None:
        model = armed_model(frozenset(range(5)))
        for index in range(4):
            model.enqueue(6, f"ch6-{index}".encode("ascii"))
        model.service_one(6, commit_result=CommitResult.UNKNOWN)
        model.service_one(6, commit_result=CommitResult.UNKNOWN)
        model.service_one(6, commit_result=CommitResult.COMMITTED)
        model.service_one(6, commit_result=CommitResult.UNKNOWN)

        model.enqueue(7, b"ch7-zero")
        model.service_one(7, commit_result=CommitResult.UNKNOWN)
        self.assertEqual(
            model.uncertain_ranges(),
            [
                {"epoch": 1, "logical_channel": 6, "start": 0, "end": 1},
                {"epoch": 1, "logical_channel": 6, "start": 3, "end": 3},
                {"epoch": 1, "logical_channel": 7, "start": 0, "end": 0},
            ],
        )


if __name__ == "__main__":
    unittest.main()
