"""Validate source-backed CF-09 A-D scenario data against the existing model."""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from cf09_bus_scheduler_model_v1 import SchedulerConfig, static_planning_screen


SCENARIO_PATH = (
    Path(__file__).resolve().parents[1]
    / "engineering"
    / "cf09-serial-source-scenarios-v1.0.json"
)


def load_scenarios() -> dict[str, object]:
    return json.loads(SCENARIO_PATH.read_text(encoding="utf-8"))


class CF09SourceScenarioTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.data = load_scenarios()
        cls.scenarios = {
            item["scenario_id"]: item for item in cls.data["scenarios"]
        }

    def test_exactly_a_through_d_scenarios_exist(self) -> None:
        self.assertEqual(
            set(self.scenarios),
            {
                "A_2_UART_PLUS_QUAD",
                "B_3_UART_PLUS_DUAL",
                "C_HELPER_MCU",
                "D_2_UART_PLUS_TWO_DUAL",
            },
        )

    def test_a_b_d_only_close_mirrored_channel_arithmetic(self) -> None:
        for scenario_id in (
            "A_2_UART_PLUS_QUAD",
            "B_3_UART_PLUS_DUAL",
            "D_2_UART_PLUS_TWO_DUAL",
        ):
            with self.subTest(scenario_id=scenario_id):
                scenario = self.scenarios[scenario_id]
                self.assertEqual(
                    scenario["local_tx_sources"]
                    + scenario["external_tx_channels_in_mirrored_5"],
                    5,
                )
                self.assertEqual(
                    scenario["mirrored_5_coverage"],
                    "ARITHMETICALLY_COMPLETE",
                )
                self.assertEqual(
                    scenario["independent_10_coverage"],
                    "INSUFFICIENT",
                )

    def test_b_is_blocked_by_uart0_reservation(self) -> None:
        scenario = self.scenarios["B_3_UART_PLUS_DUAL"]
        self.assertEqual(scenario["scenario_status"], "BLOCKED")
        self.assertFalse(scenario["uart0_reserved"])
        self.assertIn(
            "Q_UART0_CONFLICT_WITH_PROGRAMMING_RECOVERY_LOG",
            scenario["blockers"],
        )

    def test_c_has_no_fabricated_capacity_values(self) -> None:
        scenario = self.scenarios["C_HELPER_MCU"]
        self.assertEqual(scenario["scenario_status"], "UNKNOWN")
        self.assertEqual(scenario["reference_candidates"], [])
        for key in (
            "local_tx_sources",
            "external_uart_channels",
            "external_tx_channels_in_mirrored_5",
            "rx_policy",
            "mirrored_5_coverage",
            "independent_10_coverage",
        ):
            self.assertEqual(scenario[key], "UNKNOWN")

    def test_candidate_channel_fifo_and_spi_values_are_locked(self) -> None:
        candidates = self.data["reference_candidates"]
        self.assertEqual(
            (candidates["MAX14830"]["uart_channels"], candidates["MAX14830"]["fifo_entries_per_direction_per_uart"], candidates["MAX14830"]["spi_max_hz"]),
            (4, 128, 26_000_000),
        )
        self.assertEqual(
            (candidates["MAX3109"]["uart_channels"], candidates["MAX3109"]["fifo_entries_per_direction_per_uart"], candidates["MAX3109"]["spi_max_hz"]),
            (2, 128, 26_000_000),
        )
        self.assertEqual(
            (candidates["SC16IS762"]["uart_channels"], candidates["SC16IS762"]["fifo_entries_per_direction_per_uart"], candidates["SC16IS762"]["spi_max_hz"]),
            (2, 64, 15_000_000),
        )

    def test_every_numeric_sensitivity_row_reproduces_exactly(self) -> None:
        assumptions = self.data["static_model_assumptions"]
        for row in self.data["sensitivity_rows"]:
            with self.subTest(row_id=row["row_id"]):
                result = static_planning_screen(
                    SchedulerConfig(
                        external_channels=row["external_tx_channels"],
                        uart_baud=row["uart_baud"],
                        host_bus_bps=row["host_bus_bps"],
                        frame_bits=assumptions["frame_bits"],
                        duty_numerator=assumptions["duty_numerator"],
                        duty_denominator=assumptions["duty_denominator"],
                        host_bits_per_uart_entry=assumptions["host_bits_per_uart_entry"],
                        guard_numerator=assumptions["guard_numerator"],
                        guard_denominator=assumptions["guard_denominator"],
                        planning_limit_numerator=assumptions["planning_limit_numerator"],
                        planning_limit_denominator=assumptions["planning_limit_denominator"],
                        fifo_capacity_entries=row["fifo_entries"],
                    )
                )
                self.assertEqual(result.required_bus_bps.denominator, 1)
                self.assertEqual(result.required_bus_bps.numerator, row["required_bus_bps"])
                self.assertEqual(
                    f"{float(result.utilization * 100):.6f}",
                    row["utilization_percent"],
                )
                self.assertEqual(
                    f"{float(result.fifo_full_time_us):.6f}",
                    row["fifo_full_time_us"],
                )
                self.assertEqual(result.verdict.value, row["screen_verdict"])

    def test_sensitivity_does_not_claim_hardware_pass(self) -> None:
        self.assertIn("NO_HARDWARE_CAPACITY_PASS", self.data["claim_boundary"])
        for row in self.data["sensitivity_rows"]:
            self.assertNotEqual(row["screen_verdict"], "PASS")

    def test_power_off_status_never_claims_pass(self) -> None:
        for candidate in self.data["reference_candidates"].values():
            self.assertIn("UNKNOWN", candidate["power_off_backfeed"])

    def test_driver_references_preserve_license_and_claim_boundaries(self) -> None:
        drivers = self.data["driver_ecosystem"]
        self.assertIn(
            "NO_IN_TREE",
            drivers["esp_idf"]["external_bridge_driver_status"],
        )
        self.assertEqual(
            drivers["sc16is750_esp_idf_prototype"]["license"],
            "MIT",
        )
        self.assertIn(
            "PROTOTYPE_SCAFFOLD_ONLY",
            drivers["sc16is750_esp_idf_prototype"]["status"],
        )
        self.assertEqual(
            drivers["linux_behavior_references"]["license"],
            "GPL-2.0+",
        )
        self.assertIn(
            "NOT_COPY_SOURCE",
            drivers["linux_behavior_references"]["status"],
        )
        helper = drivers["helper_mcu_references"]
        self.assertEqual(helper["ch32v307_uart_count"], 8)
        self.assertEqual(
            helper["ch32v307_independent_10_coverage"],
            "INSUFFICIENT",
        )


if __name__ == "__main__":
    unittest.main()
