"""Cross-check the CF-09 physical-port inventory against the executable host model."""

from __future__ import annotations

import json
import unittest
from pathlib import Path

from cf09_host_model_v1 import Topology, build_routes


INVENTORY_PATH = (
    Path(__file__).resolve().parents[1]
    / "engineering"
    / "cf09-serial-port-inventory-v1.0.json"
)
DOCUMENT_PATH = Path(__file__).resolve().parents[1] / "docs" / "serial-port-inventory.md"


def load_inventory() -> dict[str, object]:
    return json.loads(INVENTORY_PATH.read_text(encoding="utf-8"))


class CF09PortInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.inventory = load_inventory()
        cls.ports = cls.inventory["external_ports"]
        cls.by_id = {port["port_id"]: port for port in cls.ports}

    def test_inventory_has_exactly_eleven_unique_external_ports(self) -> None:
        self.assertEqual(self.inventory["external_port_count"], 11)
        self.assertEqual(len(self.ports), 11)
        self.assertEqual(len(self.by_id), 11)
        self.assertEqual(
            set(self.by_id),
            {f"TTL{i}" for i in range(5)}
            | {f"RS232{i}" for i in range(5)}
            | {"RX0"},
        )

    def test_direction_counts_are_ten_tx_and_one_rx(self) -> None:
        self.assertEqual(
            sum(port["direction"] == "TX" for port in self.ports),
            10,
        )
        self.assertEqual(
            sum(port["direction"] == "RX" for port in self.ports),
            1,
        )

    def test_document_skeleton_and_machine_inventory_have_same_port_ids(self) -> None:
        document_ids = {
            line.split("|")[1].strip()
            for line in DOCUMENT_PATH.read_text(encoding="utf-8").splitlines()
            if line.startswith(("| TTL", "| RS232", "| RX0"))
        }
        self.assertEqual(document_ids, set(self.by_id))

    def test_mirrored_inventory_matches_executable_routes(self) -> None:
        routes = build_routes(Topology.mirrored())
        for logical_channel, route in routes.items():
            for endpoint in route.endpoints:
                declared = self.by_id[endpoint]["mirrored_5"]
                self.assertEqual(declared["logical_channel"], logical_channel)
                self.assertEqual(declared["serializer"], route.serializer)
                self.assertEqual(declared["resource"], route.resource)

    def test_independent_inventory_matches_executable_routes(self) -> None:
        routes = build_routes(Topology.independent())
        for logical_channel, route in routes.items():
            for endpoint in route.endpoints:
                declared = self.by_id[endpoint]["independent_10"]
                self.assertEqual(declared["logical_channel"], logical_channel)
                self.assertEqual(declared["serializer"], route.serializer)
                self.assertEqual(declared["resource"], route.resource)

    def test_mirrored_pairs_share_one_logical_channel_and_serializer(self) -> None:
        for index in range(5):
            ttl = self.by_id[f"TTL{index}"]["mirrored_5"]
            rs232 = self.by_id[f"RS232{index}"]["mirrored_5"]
            self.assertEqual(ttl, rs232)

    def test_independent_tx_endpoints_have_ten_unique_channels_and_serializers(self) -> None:
        routes = [
            port["independent_10"]
            for port in self.ports
            if port["direction"] == "TX"
        ]
        self.assertEqual({route["logical_channel"] for route in routes}, set(range(10)))
        self.assertEqual({route["serializer"] for route in routes}, {f"S{i}" for i in range(10)})

    def test_reserved_and_constrained_gpio_are_not_credited(self) -> None:
        policy = self.inventory["resource_policy"]
        self.assertEqual(policy["uart0"]["gpio"], [43, 44])
        self.assertEqual(policy["uart0"]["cf09_capacity_credit"], 0)
        self.assertEqual(policy["usb_differential"]["gpio"], [19, 20])
        self.assertEqual(policy["usb_differential"]["cf09_capacity_credit"], 0)
        self.assertEqual(policy["boot_constrained_gpio"], [3, 45])
        self.assertEqual(policy["conditional_pad_jtag_gpio"], [39, 40, 41, 42])
        self.assertEqual(policy["rmt_tx_capacity_credit"], 0)

    def test_inventory_does_not_claim_exact_parts_or_final_pins(self) -> None:
        self.assertIn("NO_EXACT_PART_SELECTION", self.inventory["claim_boundary"])
        self.assertIn("NO_PIN_FREEZE", self.inventory["claim_boundary"])
        for port in self.ports:
            exact_key = "exact_receiver" if port["direction"] == "RX" else "exact_driver"
            self.assertEqual(port[exact_key], "UNKNOWN")
            self.assertEqual(port["connector_pinout"], "UNKNOWN")

    def test_rx_stays_conditional_in_both_topologies(self) -> None:
        rx = self.by_id["RX0"]
        self.assertEqual(rx["electrical_class"], "UNKNOWN")
        self.assertEqual(rx["mirrored_5"]["resource"], "CONDITIONAL")
        self.assertEqual(rx["independent_10"]["resource"], "CONDITIONAL")
        self.assertEqual(rx["mirrored_5"]["logical_channel"], "UNKNOWN")
        self.assertEqual(rx["independent_10"]["logical_channel"], "UNKNOWN")


if __name__ == "__main__":
    unittest.main()
