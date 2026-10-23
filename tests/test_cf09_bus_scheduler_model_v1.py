"""Regression tests for the CF-09 synthetic bus/FIFO/IRQ scheduler model."""

from __future__ import annotations

from fractions import Fraction
import unittest

from cf09_bus_scheduler_model_v1 import (
    CAPACITY_CLAIM,
    FIFO_ENTRY_POLICY,
    HARDWARE_STATUS,
    INITIAL_FIFO_POLICY,
    MODEL_CLASS,
    STATIC_SCREEN_SCOPE,
    TIE_BREAK_POLICY,
    UTILIZATION_SCOPE,
    DeadlineStatus,
    PlanningVerdict,
    REFERENCE_VECTORS,
    RunStatus,
    SchedulerConfig,
    fifo_full_time_us,
    reference_static_screens,
    simulate,
    static_planning_screen,
)


class Cf09StaticPlanningScreenTests(unittest.TestCase):
    def test_scope_labels_forbid_hardware_capacity_claim(self) -> None:
        self.assertEqual(MODEL_CLASS, "ASSUMPTION / MODEL_ONLY")
        self.assertEqual(HARDWARE_STATUS, "NOT_RUN / HARDWARE_REQUIRED")
        self.assertEqual(CAPACITY_CLAIM, "NO_HARDWARE_CAPACITY_PASS")
        self.assertIn("ONE_ENTRY", FIFO_ENTRY_POLICY)
        self.assertIn("PREFILL", INITIAL_FIFO_POLICY)
        self.assertIn("ARRIVAL_THEN_FIFO_DRAIN", TIE_BREAK_POLICY)
        self.assertIn("EXCLUDES", STATIC_SCREEN_SCOPE)
        self.assertIn("REPORTED_SEPARATELY", UTILIZATION_SCOPE)
        self.assertNotIn("PASS", {status.name for status in RunStatus})

    def test_reference_control_points_are_exact(self) -> None:
        expected = {
            "MIRRORED_5_I2C_1M_115200": (
                691_200,
                PlanningVerdict.SCREEN_CANDIDATE,
            ),
            "INDEPENDENT_10_I2C_1M_115200": (
                1_843_200,
                PlanningVerdict.SCREEN_REJECT,
            ),
            "INDEPENDENT_10_SPI_4M_115200": (
                1_843_200,
                PlanningVerdict.SCREEN_CANDIDATE,
            ),
            "INDEPENDENT_10_SPI_15M_1M": (
                16_000_000,
                PlanningVerdict.SCREEN_REJECT,
            ),
            "INDEPENDENT_10_SPI_26M_1M": (
                16_000_000,
                PlanningVerdict.SCREEN_CANDIDATE,
            ),
            "ALL_EXTERNAL_10_SPI_4M_115200": (
                2_304_000,
                PlanningVerdict.SCREEN_CANDIDATE,
            ),
            "ALL_EXTERNAL_10_SPI_26M_1M": (
                20_000_000,
                PlanningVerdict.SCREEN_REJECT,
            ),
        }
        vectors = {vector.name: vector for vector in REFERENCE_VECTORS}
        screens = reference_static_screens()
        self.assertEqual(set(screens), set(expected))
        for name, (required_bps, verdict) in expected.items():
            with self.subTest(name=name):
                self.assertEqual(
                    screens[name].required_bus_bps,
                    Fraction(required_bps, 1),
                )
                self.assertEqual(
                    screens[name].utilization,
                    Fraction(required_bps, vectors[name].host_bus_bps),
                )
                self.assertEqual(screens[name].verdict, verdict)

    def test_fifo_service_horizons_are_exact(self) -> None:
        self.assertEqual(fifo_full_time_us(64, 10, 115_200), Fraction(50_000, 9))
        self.assertEqual(fifo_full_time_us(64, 10, 1_000_000), Fraction(640, 1))
        for invalid in ((0, 10, 1), (1, 0, 1), (1, 10, 0)):
            with self.subTest(invalid=invalid):
                with self.assertRaises(ValueError):
                    fifo_full_time_us(*invalid)

    def test_duty_changes_arrivals_not_serializer_character_time(self) -> None:
        full = SchedulerConfig(1, 1_000_000, 4_000_000)
        half = SchedulerConfig(
            1,
            1_000_000,
            4_000_000,
            duty_numerator=1,
            duty_denominator=2,
        )
        self.assertEqual(full.serializer_character_period_us, Fraction(10, 1))
        self.assertEqual(half.serializer_character_period_us, Fraction(10, 1))
        self.assertEqual(full.source_arrival_period_us, Fraction(10, 1))
        self.assertEqual(half.source_arrival_period_us, Fraction(20, 1))

    def test_guarded_transaction_time_is_exact_and_gap_is_separate(self) -> None:
        config = SchedulerConfig(
            1,
            1_000,
            1_000_000,
            transaction_overhead_bits=8,
            inter_transaction_gap_us=100,
        )
        self.assertEqual(config.transaction_time_us(1), Fraction(30, 1))
        self.assertEqual(config.transaction_time_us(4), Fraction(90, 1))
        with self.assertRaises(ValueError):
            config.transaction_time_us(0)

    def test_static_screen_explicitly_ignores_gap_and_protocol_overhead(self) -> None:
        baseline = SchedulerConfig(1, 115_200, 4_000_000)
        detailed = SchedulerConfig(
            1,
            115_200,
            4_000_000,
            transaction_overhead_bits=37,
            inter_transaction_gap_us=19,
            irq_service_latency_us=23,
        )
        self.assertEqual(static_planning_screen(baseline), static_planning_screen(detailed))

    def test_invalid_configuration_is_fail_closed(self) -> None:
        base = {
            "external_channels": 1,
            "uart_baud": 115_200,
            "host_bus_bps": 4_000_000,
        }
        invalid_overrides = (
            {"external_channels": 0},
            {"external_channels": True},
            {"duty_numerator": 2, "duty_denominator": 1},
            {"planning_limit_numerator": 2, "planning_limit_denominator": 1},
            {"fifo_capacity_entries": 8, "burst_entries": 9},
            {"refill_trigger_free_entries": 0},
            {"host_queue_limit_entries": 0},
            {"deadline_us": 0},
            {"initial_fifo_fill_entries": 65},
            {"transaction_overhead_bits": -1},
            {"arrival_stop_us": -1},
            {"horizon_us": 100, "arrival_stop_us": 101},
        )
        for override in invalid_overrides:
            with self.subTest(override=override):
                args = dict(base)
                args.update(override)
                with self.assertRaises(ValueError):
                    SchedulerConfig(**args)


class Cf09DiscreteEventTests(unittest.TestCase):
    def assert_conservation(self, run) -> None:  # type: ignore[no-untyped-def]
        self.assertEqual(
            run.total_generated_entries,
            run.total_accepted_entries + run.total_backpressured_entries,
        )
        self.assertEqual(
            run.total_accepted_entries,
            run.total_transmitted_stream_entries + run.final_outstanding_entries,
        )
        for channel in run.channels:
            self.assertEqual(
                channel.generated_entries,
                channel.accepted_entries + channel.backpressured_entries,
            )
            self.assertEqual(
                channel.accepted_entries,
                channel.transmitted_stream_entries
                + channel.final_outstanding_entries,
            )

    def test_candidate_vectors_have_no_visible_loss_but_remain_incomplete(self) -> None:
        configs = (
            SchedulerConfig(8, 115_200, 4_000_000, horizon_us=20_000),
            SchedulerConfig(8, 1_000_000, 26_000_000, horizon_us=10_000),
        )
        for config in configs:
            with self.subTest(config=config):
                run = simulate(config)
                self.assertEqual(run.screen.verdict, PlanningVerdict.SCREEN_CANDIDATE)
                self.assertEqual(run.status, RunStatus.MODEL_BACKLOG_PRESENT)
                self.assertEqual(
                    run.deadline_status,
                    DeadlineStatus.UNKNOWN_NO_THRESHOLD,
                )
                self.assertEqual(run.total_starvation_events, 0)
                self.assertEqual(run.total_backpressured_entries, 0)
                self.assertGreater(run.final_outstanding_entries, 0)
                self.assertLessEqual(run.service_count_span, 1)
                self.assert_conservation(run)

    def test_overloaded_vector_exposes_backpressure_and_starvation(self) -> None:
        run = simulate(
            SchedulerConfig(
                8,
                1_000_000,
                15_000_000,
                host_queue_limit_entries=32,
                horizon_us=10_000,
            )
        )
        self.assertEqual(run.screen.verdict, PlanningVerdict.SCREEN_REJECT)
        self.assertEqual(run.status, RunStatus.MODEL_VIOLATION_VISIBLE)
        self.assertGreater(run.total_backpressured_entries, 0)
        self.assertGreater(run.total_starvation_events, 0)
        self.assertTrue(any("BACKPRESSURE" in item for item in run.anomalies))
        self.assertTrue(any("FIFO_STARVATION" in item for item in run.anomalies))
        self.assert_conservation(run)

    def test_fifo_horizon_equal_irq_latency_is_not_treated_as_safe(self) -> None:
        config = SchedulerConfig(
            1,
            1_000_000,
            26_000_000,
            fifo_capacity_entries=64,
            burst_entries=64,
            refill_trigger_free_entries=64,
            irq_service_latency_us=640,
            horizon_us=2_000,
        )
        self.assertEqual(
            static_planning_screen(config).fifo_full_time_us,
            Fraction(640, 1),
        )
        run = simulate(config)
        self.assertEqual(run.status, RunStatus.MODEL_VIOLATION_VISIBLE)
        self.assertGreater(run.total_starvation_events, 0)

    def test_burst_and_fifo_sweep_changes_runway_result(self) -> None:
        results = {
            burst: simulate(
                SchedulerConfig(
                    8,
                    1_000_000,
                    26_000_000,
                    burst_entries=burst,
                    refill_trigger_free_entries=burst,
                    horizon_us=5_000,
                )
            )
            for burst in (1, 4, 16, 64)
        }
        for burst in (1, 4, 16):
            self.assertEqual(results[burst].total_starvation_events, 0)
        self.assertGreater(results[64].total_starvation_events, 0)
        larger_fifo = simulate(
            SchedulerConfig(
                8,
                1_000_000,
                26_000_000,
                fifo_capacity_entries=128,
                burst_entries=64,
                refill_trigger_free_entries=64,
                horizon_us=5_000,
            )
        )
        self.assertEqual(larger_fifo.total_starvation_events, 0)
        self.assertEqual(larger_fifo.status, RunStatus.MODEL_BACKLOG_PRESENT)

    def test_low_duty_intentional_idle_is_not_starvation(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000_000,
                4_000_000,
                duty_numerator=1,
                duty_denominator=2,
                initial_fifo_fill_entries=0,
                arrival_stop_us=100,
                horizon_us=1_000,
            )
        )
        self.assertEqual(run.total_generated_entries, 5)
        self.assertEqual(run.total_transmitted_stream_entries, 5)
        self.assertEqual(run.total_starvation_events, 0)
        self.assertEqual(run.total_idle_events, 5)
        self.assertEqual(run.status, RunStatus.MODEL_NO_VISIBLE_LOSS)

    def test_exact_edge_commit_is_conservative_and_not_backdated(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                4_000,
                frame_bits=10,
                fifo_capacity_entries=1,
                burst_entries=1,
                initial_fifo_fill_entries=1,
                arrival_stop_us=10_000,
                horizon_us=25_000,
            )
        )
        self.assertEqual(run.transactions[0].started_us, Fraction(10_000, 1))
        self.assertEqual(run.transactions[0].completes_us, Fraction(15_000, 1))
        self.assertEqual(run.channels[0].max_latency_us, Fraction(15_000, 1))
        self.assertEqual(run.total_starvation_events, 1)
        self.assertEqual(run.status, RunStatus.MODEL_VIOLATION_VISIBLE)

    def test_accepted_but_undrained_entry_cannot_false_pass_deadline(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                1_000_000,
                frame_bits=10,
                fifo_capacity_entries=2,
                burst_entries=1,
                initial_fifo_fill_entries=1,
                horizon_us=15_000,
                deadline_us=1_000,
            )
        )
        self.assertEqual(run.total_accepted_entries, 1)
        self.assertEqual(run.total_transmitted_stream_entries, 0)
        self.assertEqual(run.final_outstanding_entries, 1)
        self.assertEqual(run.total_deadline_misses, 1)
        self.assertEqual(run.deadline_status, DeadlineStatus.MODEL_THRESHOLD_MISS)
        self.assertEqual(run.status, RunStatus.MODEL_VIOLATION_VISIBLE)

    def test_incomplete_non_overdue_deadline_remains_unknown(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                1_000_000,
                fifo_capacity_entries=2,
                burst_entries=1,
                initial_fifo_fill_entries=0,
                horizon_us=15_000,
                deadline_us=10_000,
            )
        )
        self.assertEqual(run.total_deadline_misses, 0)
        self.assertEqual(run.final_outstanding_entries, 1)
        self.assertEqual(
            run.deadline_status,
            DeadlineStatus.UNKNOWN_OUTSTANDING_ENTRIES,
        )

    def test_completed_deadline_can_be_screened_only_against_caller_value(self) -> None:
        base = {
            "external_channels": 1,
            "uart_baud": 1_000,
            "host_bus_bps": 1_000_000,
            "initial_fifo_fill_entries": 0,
            "arrival_stop_us": 10_000,
            "horizon_us": 21_000,
        }
        miss = simulate(SchedulerConfig(**base, deadline_us=9_000))
        within = simulate(SchedulerConfig(**base, deadline_us=11_000))
        self.assertEqual(miss.channels[0].max_latency_us, Fraction(10_020, 1))
        self.assertEqual(miss.deadline_status, DeadlineStatus.MODEL_THRESHOLD_MISS)
        self.assertEqual(miss.status, RunStatus.MODEL_VIOLATION_VISIBLE)
        self.assertEqual(
            within.deadline_status,
            DeadlineStatus.MODEL_WITHIN_CALLER_THRESHOLD,
        )
        self.assertEqual(within.status, RunStatus.MODEL_NO_VISIBLE_LOSS)

    def test_deadline_with_dropped_load_cannot_report_within(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000_000,
                1_000_000,
                fifo_capacity_entries=1,
                burst_entries=1,
                initial_fifo_fill_entries=0,
                host_queue_limit_entries=1,
                arrival_stop_us=100,
                horizon_us=1_000,
                deadline_us=1_000,
            )
        )
        self.assertGreater(run.total_backpressured_entries, 0)
        self.assertEqual(run.total_deadline_misses, 0)
        self.assertEqual(run.status, RunStatus.MODEL_VIOLATION_VISIBLE)
        self.assertEqual(
            run.deadline_status,
            DeadlineStatus.UNKNOWN_TRAFFIC_VIOLATION,
        )

    def test_empty_vector_is_not_vacuous_success(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                115_200,
                4_000_000,
                initial_fifo_fill_entries=0,
                arrival_stop_us=0,
                horizon_us=1_000,
                deadline_us=1,
            )
        )
        self.assertEqual(run.total_generated_entries, 0)
        self.assertEqual(run.status, RunStatus.MODEL_NOT_RUN_EMPTY_VECTOR)
        self.assertEqual(run.deadline_status, DeadlineStatus.UNKNOWN_NO_SAMPLES)

    def test_wire_and_gap_reservation_are_reported_separately(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                1_000_000,
                initial_fifo_fill_entries=0,
                arrival_stop_us=10_000,
                horizon_us=30_000,
                transaction_overhead_bits=8,
                inter_transaction_gap_us=100,
            )
        )
        self.assertEqual(run.bus_wire_busy_us, Fraction(30, 1))
        self.assertEqual(run.bus_gap_reserved_us, Fraction(100, 1))
        self.assertEqual(run.full_horizon_wire_fraction, Fraction(1, 1_000))
        self.assertEqual(run.full_horizon_reserved_fraction, Fraction(13, 3_000))
        self.assertEqual(run.traffic_window_wire_fraction, Fraction(0, 1))
        self.assertEqual(run.traffic_window_reserved_fraction, Fraction(0, 1))
        self.assertGreater(
            run.full_horizon_reserved_fraction,
            run.full_horizon_wire_fraction,
        )

    def test_tail_does_not_hide_traffic_window_denominator(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                1_000_000,
                initial_fifo_fill_entries=0,
                arrival_stop_us=20_000,
                horizon_us=31_000,
            )
        )
        self.assertEqual(run.total_generated_entries, 2)
        self.assertEqual(run.total_transmitted_stream_entries, 2)
        self.assertEqual(run.bus_wire_busy_us, Fraction(40, 1))
        self.assertEqual(run.traffic_window_wire_busy_us, Fraction(20, 1))
        self.assertEqual(run.full_horizon_wire_fraction, Fraction(1, 775))
        self.assertEqual(run.traffic_window_wire_fraction, Fraction(1, 1_000))

    def test_transaction_crossing_horizon_remains_visible_and_reserved(self) -> None:
        run = simulate(
            SchedulerConfig(
                1,
                1_000,
                1_000,
                fifo_capacity_entries=1,
                burst_entries=1,
                initial_fifo_fill_entries=0,
                arrival_stop_us=10_000,
                horizon_us=15_000,
            )
        )
        self.assertEqual(len(run.transactions), 1)
        transaction = run.transactions[0]
        self.assertEqual(transaction.started_us, Fraction(10_000, 1))
        self.assertEqual(transaction.completes_us, Fraction(30_000, 1))
        self.assertEqual(run.terminal_inflight_transaction, transaction)
        self.assertEqual(run.bus_wire_busy_us, Fraction(5_000, 1))
        self.assertEqual(run.full_horizon_wire_fraction, Fraction(1, 3))
        self.assertEqual(run.channels[0].bus_transactions, 0)
        self.assertEqual(run.channels[0].bus_payload_entries, 0)
        self.assertEqual(run.channels[0].final_host_queue_entries, 0)
        self.assertEqual(run.channels[0].final_reserved_fifo_entries, 1)
        self.assertEqual(run.final_outstanding_entries, 1)
        self.assertEqual(run.status, RunStatus.MODEL_BACKLOG_PRESENT)
        self.assertTrue(any("TERMINAL_INFLIGHT" in item for item in run.anomalies))

    def test_global_bus_intervals_and_fifo_reservations_never_overlap(self) -> None:
        config = SchedulerConfig(
            8,
            1_000_000,
            26_000_000,
            inter_transaction_gap_us=2,
            horizon_us=5_000,
        )
        run = simulate(config)
        self.assertGreater(len(run.transactions), 1)
        for previous, current in zip(run.transactions, run.transactions[1:]):
            self.assertLessEqual(
                previous.completes_us + config.inter_transaction_gap_us,
                current.started_us,
            )
        for transaction in run.transactions:
            self.assertLessEqual(
                transaction.fifo_entries_at_start
                + transaction.reserved_entries_at_start,
                config.fifo_capacity_entries,
            )
            self.assertLessEqual(
                transaction.payload_entries,
                config.burst_entries,
            )
            self.assertEqual(
                transaction.completes_us - transaction.started_us,
                config.transaction_time_us(transaction.payload_entries),
            )

    def test_run_is_deterministic(self) -> None:
        config = SchedulerConfig(
            3,
            115_200,
            1_000_000,
            fifo_capacity_entries=64,
            burst_entries=4,
            refill_trigger_free_entries=4,
            horizon_us=5_000,
        )
        self.assertEqual(simulate(config), simulate(config))


if __name__ == "__main__":
    unittest.main()
