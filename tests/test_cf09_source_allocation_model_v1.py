"""Tests for the exact-part-neutral CF-09 A-D source allocation model."""

from __future__ import annotations

import unittest
from pathlib import Path

from cf09_source_allocation_model_v1 import (
    AllocationStatus,
    allocate_mirrored_5,
    load_scenario_set,
)


SCENARIO_PATH = (
    Path(__file__).resolve().parents[1]
    / "engineering"
    / "cf09-serial-source-scenarios-v1.0.json"
)


class CF09SourceAllocationModelTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        data = load_scenario_set(SCENARIO_PATH)
        cls.scenarios = {
            item["scenario_id"]: item for item in data["scenarios"]
        }

    def test_a_uses_two_local_three_quad_tx_and_dedicated_rx(self) -> None:
        result = allocate_mirrored_5(self.scenarios["A_2_UART_PLUS_QUAD"])
        self.assertEqual(result.status, AllocationStatus.PROVISIONAL)
        self.assertEqual(
            [item.resource.resource_id for item in result.tx],
            ["UART1", "UART2", "BRIDGE_A_CH0", "BRIDGE_A_CH1", "BRIDGE_A_CH2"],
        )
        self.assertEqual(result.rx.resource.resource_id, "BRIDGE_A_CH3")
        self.assertEqual(result.rx.relationship, "DEDICATED_UART_CHANNEL")

    def test_b_channel_arithmetic_does_not_clear_uart0_blocker(self) -> None:
        result = allocate_mirrored_5(self.scenarios["B_3_UART_PLUS_DUAL"])
        self.assertEqual(len(result.tx), 5)
        self.assertEqual(result.tx[0].resource.resource_id, "UART0")
        self.assertEqual(result.status, AllocationStatus.BLOCKED)
        self.assertIn(
            "Q_UART0_CONFLICT_WITH_PROGRAMMING_RECOVERY_LOG",
            result.anomalies,
        )
        self.assertEqual(
            result.rx.relationship,
            "SHARED_TX_RX_CONFIGURATION_CONDITIONAL",
        )

    def test_c_refuses_to_invent_helper_resources(self) -> None:
        result = allocate_mirrored_5(self.scenarios["C_HELPER_MCU"])
        self.assertEqual(result.status, AllocationStatus.NOT_RUN)
        self.assertEqual(result.tx, ())
        self.assertIsNone(result.rx.resource)
        self.assertEqual(result.independent_10_status, "UNKNOWN")

    def test_d_spans_two_dual_bridges_and_reserves_second_bridge_rx(self) -> None:
        result = allocate_mirrored_5(
            self.scenarios["D_2_UART_PLUS_TWO_DUAL"]
        )
        self.assertEqual(result.status, AllocationStatus.PROVISIONAL)
        self.assertEqual(
            [item.resource.resource_id for item in result.tx],
            ["UART1", "UART2", "BRIDGE_A_CH0", "BRIDGE_A_CH1", "BRIDGE_B_CH0"],
        )
        self.assertEqual(result.rx.resource.resource_id, "BRIDGE_B_CH1")
        self.assertEqual(result.rx.relationship, "DEDICATED_UART_CHANNEL")

    def test_all_known_classes_cover_every_mirrored_endpoint_once(self) -> None:
        expected = {f"TTL{i}" for i in range(5)} | {f"RS232{i}" for i in range(5)}
        for scenario_id in (
            "A_2_UART_PLUS_QUAD",
            "B_3_UART_PLUS_DUAL",
            "D_2_UART_PLUS_TWO_DUAL",
        ):
            with self.subTest(scenario_id=scenario_id):
                result = allocate_mirrored_5(self.scenarios[scenario_id])
                endpoints = {
                    endpoint for allocation in result.tx for endpoint in allocation.endpoints
                }
                self.assertEqual(endpoints, expected)

    def test_none_of_a_b_d_can_supply_ten_independent_tx_resources(self) -> None:
        for scenario_id in (
            "A_2_UART_PLUS_QUAD",
            "B_3_UART_PLUS_DUAL",
            "D_2_UART_PLUS_TWO_DUAL",
        ):
            with self.subTest(scenario_id=scenario_id):
                result = allocate_mirrored_5(self.scenarios[scenario_id])
                self.assertTrue(
                    result.independent_10_status.startswith(
                        "BLOCKED_BY_RESOURCE_COUNT"
                    )
                )
                self.assertNotEqual(result.status.value, "PASS")


if __name__ == "__main__":
    unittest.main()
