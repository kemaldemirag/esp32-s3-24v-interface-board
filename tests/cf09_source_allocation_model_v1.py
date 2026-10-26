"""Exact-part-neutral CF-09 A-D source allocation model.

The model consumes the source-class scenario file and proves only channel
arithmetic and deterministic logical-to-resource allocation.  It does not
model an actual bridge register map, GPIO assignment, timing, electrical
behaviour or connector delivery.  A blocked scenario remains blocked even if
its channel arithmetic is complete.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from enum import Enum
from pathlib import Path


MODEL_CLASS = "DERIVED / MODEL_ONLY"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"


class AllocationStatus(str, Enum):
    PROVISIONAL = "PROVISIONAL_CHANNEL_ARITHMETIC_ONLY"
    BLOCKED = "BLOCKED_BY_SCENARIO_POLICY"
    NOT_RUN = "NOT_RUN_UNKNOWN_RESOURCE_CLASS"


@dataclass(frozen=True)
class UartResource:
    resource_id: str
    device_id: str
    channel_index: int | None
    local: bool


@dataclass(frozen=True)
class TxAllocation:
    logical_channel: int
    resource: UartResource
    endpoints: tuple[str, str]


@dataclass(frozen=True)
class RxAllocation:
    resource: UartResource | None
    relationship: str


@dataclass(frozen=True)
class AllocationResult:
    scenario_id: str
    status: AllocationStatus
    tx: tuple[TxAllocation, ...]
    rx: RxAllocation
    independent_10_status: str
    anomalies: tuple[str, ...]


def load_scenario_set(path: Path) -> dict[str, object]:
    return json.loads(path.read_text(encoding="utf-8"))


def _resources(scenario: dict[str, object]) -> tuple[UartResource, ...]:
    local = tuple(
        UartResource(name, "ESP32_S3", None, True)
        for name in scenario["local_uart_resources"]
    )
    external = tuple(
        UartResource(
            f"{device['device_id']}_CH{channel}",
            device["device_id"],
            channel,
            False,
        )
        for device in scenario["external_devices"]
        for channel in range(device["uart_channels"])
    )
    return local + external


def allocate_mirrored_5(scenario: dict[str, object]) -> AllocationResult:
    scenario_id = scenario["scenario_id"]
    if scenario["mirrored_5_coverage"] == "UNKNOWN":
        return AllocationResult(
            scenario_id=scenario_id,
            status=AllocationStatus.NOT_RUN,
            tx=(),
            rx=RxAllocation(None, "UNKNOWN"),
            independent_10_status="UNKNOWN",
            anomalies=("RESOURCE_CLASS_UNKNOWN",),
        )

    resources = _resources(scenario)
    anomalies: list[str] = []
    if len(resources) < 5:
        anomalies.append(f"TX_RESOURCE_SHORTFALL:required=5:available={len(resources)}")
        tx_resources = resources
    else:
        tx_resources = resources[:5]

    tx = tuple(
        TxAllocation(
            logical_channel=index,
            resource=resource,
            endpoints=(f"TTL{index}", f"RS232{index}"),
        )
        for index, resource in enumerate(tx_resources)
    )

    unused = resources[5:]
    if unused:
        rx = RxAllocation(unused[0], "DEDICATED_UART_CHANNEL")
    elif tx:
        rx = RxAllocation(tx[-1].resource, "SHARED_TX_RX_CONFIGURATION_CONDITIONAL")
        anomalies.append("RX_CONFIGURATION_RELATION_REQUIRES_Q_C13")
    else:
        rx = RxAllocation(None, "UNKNOWN")
        anomalies.append("RX_RESOURCE_UNKNOWN")

    if scenario["scenario_status"] == "BLOCKED":
        status = AllocationStatus.BLOCKED
        anomalies.extend(scenario["blockers"])
    elif anomalies and len(tx) < 5:
        status = AllocationStatus.BLOCKED
    else:
        status = AllocationStatus.PROVISIONAL

    independent_10_status = (
        "PROVISIONAL_CHANNEL_ARITHMETIC_ONLY"
        if len(resources) >= 10
        else f"BLOCKED_BY_RESOURCE_COUNT:required_tx=10:available_uart_channels={len(resources)}"
    )
    return AllocationResult(
        scenario_id=scenario_id,
        status=status,
        tx=tx,
        rx=rx,
        independent_10_status=independent_10_status,
        anomalies=tuple(dict.fromkeys(anomalies)),
    )
