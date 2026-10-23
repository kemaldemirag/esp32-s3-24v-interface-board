"""Parameterized CF-09 SPI/FIFO/IRQ scheduler sensitivity model.

This module is ASSUMPTION / MODEL_ONLY.  It extends the legacy static CF-09
planning screen with a deterministic discrete-event model for one shared host
bus, per-channel TX FIFOs, level-like refill requests, bounded host queues and
a round-robin scheduler.  It does not model any exact bridge register protocol.

Source-backed sensitivity inputs from the CF-09 v1.2 architecture/test plan:

* 8N1 sensitivity: 10 UART frame bits.
* 9_600, 115_200 and 1_000_000 baud are test points, not requirements.
* One FIFO entry carries one UART payload octet in this model.  Exact-part FIFO
  units remain UNKNOWN.
* 16 host-bus bits per FIFO entry, 5/4 guard and 70% planning occupancy are
  portfolio assumptions, not component properties.
* 64/128 FIFO entries and 1/4/16/64-byte bursts are sensitivity points.

Duty controls offered source arrivals; it never changes the physical UART
character time.  An empty serializer with no pending accepted demand is idle,
not an underrun.  A starvation event is counted only when a serializer finishes
an entry, its FIFO is empty, and accepted demand is still waiting.  When a FIFO
source arrival, FIFO drain and host-bus commit share a timestamp, that order is
used: exact-edge demand is visible but exact-edge refill is not credited.

The model never emits a hardware capacity PASS.  ``MODEL_NO_VISIBLE_LOSS``
means only that the selected synthetic run produced no modeled underrun,
backpressure or caller-supplied deadline miss.  Exact SPI framing, FIFO
semantics, IRQ behaviour, retry traffic, RX/status traffic, CPU load and all
electrical behaviour remain UNKNOWN / HARDWARE_REQUIRED / NOT_RUN.
"""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
from enum import Enum
from fractions import Fraction
from typing import Deque


MODEL_CLASS = "ASSUMPTION / MODEL_ONLY"
HARDWARE_STATUS = "NOT_RUN / HARDWARE_REQUIRED"
CAPACITY_CLAIM = "NO_HARDWARE_CAPACITY_PASS"
SCHEDULER_POLICY = "ROUND_ROBIN / LEVEL_LIKE_REFILL_REQUEST"
INITIAL_FIFO_POLICY = "ASSUMPTION: PREFILL_BEFORE_OUTPUT_ENABLE"
FIFO_ENTRY_POLICY = "ASSUMPTION: ONE_ENTRY_EQUALS_ONE_UART_PAYLOAD_OCTET"
TIE_BREAK_POLICY = "CONSERVATIVE: ARRIVAL_THEN_FIFO_DRAIN_THEN_BUS_COMMIT"
STATIC_SCREEN_SCOPE = "LEGACY_STATIC_TX_ONLY; EXCLUDES_GAP_IRQ_STATUS_RX_RETRY_FAULT"
UTILIZATION_SCOPE = (
    "FULL_HORIZON_AND_TRAFFIC_WINDOW_REPORTED_SEPARATELY; "
    "NEITHER_IS_HARDWARE_CAPACITY_PASS"
)


class PlanningVerdict(str, Enum):
    SCREEN_CANDIDATE = "SCREEN_CANDIDATE_NOT_CAPACITY_PASS"
    SCREEN_REJECT = "SCREEN_REJECT"


class RunStatus(str, Enum):
    MODEL_NOT_RUN_EMPTY_VECTOR = "MODEL_NOT_RUN_EMPTY_VECTOR"
    MODEL_NO_VISIBLE_LOSS = "MODEL_NO_VISIBLE_LOSS"
    MODEL_BACKLOG_PRESENT = "MODEL_BACKLOG_PRESENT"
    MODEL_SCREEN_REJECTED_NO_VISIBLE_LOSS_YET = (
        "MODEL_SCREEN_REJECTED_NO_VISIBLE_LOSS_YET"
    )
    MODEL_VIOLATION_VISIBLE = "MODEL_VIOLATION_VISIBLE"


class DeadlineStatus(str, Enum):
    UNKNOWN_NO_THRESHOLD = "UNKNOWN_NO_THRESHOLD"
    UNKNOWN_NO_SAMPLES = "UNKNOWN_NO_SAMPLES"
    UNKNOWN_TRAFFIC_VIOLATION = "UNKNOWN_TRAFFIC_VIOLATION"
    UNKNOWN_OUTSTANDING_ENTRIES = "UNKNOWN_OUTSTANDING_ENTRIES"
    MODEL_WITHIN_CALLER_THRESHOLD = "MODEL_WITHIN_CALLER_THRESHOLD"
    MODEL_THRESHOLD_MISS = "MODEL_THRESHOLD_MISS"


@dataclass(frozen=True)
class SchedulerConfig:
    external_channels: int
    uart_baud: int
    host_bus_bps: int
    frame_bits: int = 10
    duty_numerator: int = 1
    duty_denominator: int = 1
    host_bits_per_uart_entry: int = 16
    guard_numerator: int = 5
    guard_denominator: int = 4
    planning_limit_numerator: int = 7
    planning_limit_denominator: int = 10
    fifo_capacity_entries: int = 64
    burst_entries: int = 16
    refill_trigger_free_entries: int | None = None
    transaction_overhead_bits: int = 0
    irq_service_latency_us: int = 0
    inter_transaction_gap_us: int = 0
    horizon_us: int = 50_000
    host_queue_limit_entries: int | None = None
    deadline_us: int | None = None
    initial_fifo_fill_entries: int | None = None
    arrival_stop_us: int | None = None

    def __post_init__(self) -> None:
        positive = {
            "external_channels": self.external_channels,
            "uart_baud": self.uart_baud,
            "host_bus_bps": self.host_bus_bps,
            "frame_bits": self.frame_bits,
            "duty_numerator": self.duty_numerator,
            "duty_denominator": self.duty_denominator,
            "host_bits_per_uart_entry": self.host_bits_per_uart_entry,
            "guard_numerator": self.guard_numerator,
            "guard_denominator": self.guard_denominator,
            "planning_limit_numerator": self.planning_limit_numerator,
            "planning_limit_denominator": self.planning_limit_denominator,
            "fifo_capacity_entries": self.fifo_capacity_entries,
            "burst_entries": self.burst_entries,
            "horizon_us": self.horizon_us,
        }
        for name, value in positive.items():
            if isinstance(value, bool) or not isinstance(value, int) or value < 1:
                raise ValueError(f"{name} must be a positive integer")

        if self.duty_numerator > self.duty_denominator:
            raise ValueError("duty cycle must be in the interval (0, 1]")
        if self.planning_limit_numerator > self.planning_limit_denominator:
            raise ValueError("planning limit must be in the interval (0, 1]")
        if self.burst_entries > self.fifo_capacity_entries:
            raise ValueError("burst_entries cannot exceed fifo_capacity_entries")

        nonnegative = {
            "transaction_overhead_bits": self.transaction_overhead_bits,
            "irq_service_latency_us": self.irq_service_latency_us,
            "inter_transaction_gap_us": self.inter_transaction_gap_us,
        }
        for name, value in nonnegative.items():
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise ValueError(f"{name} must be a non-negative integer")

        trigger = self.refill_trigger_free_entries
        if trigger is None:
            trigger = self.burst_entries
            object.__setattr__(self, "refill_trigger_free_entries", trigger)
        if isinstance(trigger, bool) or not isinstance(trigger, int):
            raise ValueError("refill_trigger_free_entries must be an integer")
        if not 1 <= trigger <= self.fifo_capacity_entries:
            raise ValueError("refill trigger must be within the FIFO capacity")

        queue_limit = self.host_queue_limit_entries
        if queue_limit is not None and (
            isinstance(queue_limit, bool)
            or not isinstance(queue_limit, int)
            or queue_limit < 1
        ):
            raise ValueError("host_queue_limit_entries must be positive or None")

        deadline = self.deadline_us
        if deadline is not None and (
            isinstance(deadline, bool)
            or not isinstance(deadline, int)
            or deadline < 1
        ):
            raise ValueError("deadline_us must be positive or None")

        initial_fill = self.initial_fifo_fill_entries
        if initial_fill is None:
            initial_fill = self.fifo_capacity_entries
            object.__setattr__(self, "initial_fifo_fill_entries", initial_fill)
        if isinstance(initial_fill, bool) or not isinstance(initial_fill, int):
            raise ValueError("initial_fifo_fill_entries must be an integer")
        if not 0 <= initial_fill <= self.fifo_capacity_entries:
            raise ValueError("initial FIFO fill must be within the FIFO capacity")

        arrival_stop = self.arrival_stop_us
        if arrival_stop is None:
            arrival_stop = self.horizon_us
            object.__setattr__(self, "arrival_stop_us", arrival_stop)
        if (
            isinstance(arrival_stop, bool)
            or not isinstance(arrival_stop, int)
            or not 0 <= arrival_stop <= self.horizon_us
        ):
            raise ValueError("arrival_stop_us must be within [0, horizon_us]")

    @property
    def duty(self) -> Fraction:
        return Fraction(self.duty_numerator, self.duty_denominator)

    @property
    def guard(self) -> Fraction:
        return Fraction(self.guard_numerator, self.guard_denominator)

    @property
    def planning_limit(self) -> Fraction:
        return Fraction(
            self.planning_limit_numerator,
            self.planning_limit_denominator,
        )

    @property
    def source_arrival_period_us(self) -> Fraction:
        return Fraction(
            self.frame_bits * self.duty_denominator * 1_000_000,
            self.uart_baud * self.duty_numerator,
        )

    @property
    def serializer_character_period_us(self) -> Fraction:
        return Fraction(self.frame_bits * 1_000_000, self.uart_baud)

    def transaction_time_us(self, payload_entries: int) -> Fraction:
        """Return guarded synthetic wire time; gap and IRQ delay are separate."""
        if payload_entries < 1:
            raise ValueError("payload_entries must be positive")
        unguarded_bits = (
            self.transaction_overhead_bits
            + payload_entries * self.host_bits_per_uart_entry
        )
        return Fraction(
            unguarded_bits * self.guard_numerator * 1_000_000,
            self.guard_denominator * self.host_bus_bps,
        )


@dataclass(frozen=True)
class StaticPlanningScreen:
    required_bus_bps: Fraction
    utilization: Fraction
    planning_limit: Fraction
    fifo_full_time_us: Fraction
    verdict: PlanningVerdict


@dataclass(frozen=True)
class ChannelResult:
    channel: int
    generated_entries: int
    accepted_entries: int
    backpressured_entries: int
    serializer_slots_filled: int
    transmitted_stream_entries: int
    starvation_events: int
    idle_events: int
    final_host_queue_entries: int
    queue_high_water_entries: int
    final_fifo_entries: int
    final_reserved_fifo_entries: int
    final_stream_fifo_entries: int
    final_outstanding_entries: int
    minimum_fifo_entries: int
    bus_transactions: int
    bus_payload_entries: int
    max_latency_us: Fraction | None
    completed_deadline_misses: int
    overdue_outstanding_entries: int
    max_service_gap_us: Fraction | None


@dataclass(frozen=True)
class SchedulerRun:
    config: SchedulerConfig
    screen: StaticPlanningScreen
    status: RunStatus
    deadline_status: DeadlineStatus
    channels: tuple[ChannelResult, ...]
    bus_wire_busy_us: Fraction
    bus_gap_reserved_us: Fraction
    traffic_window_wire_busy_us: Fraction
    traffic_window_gap_reserved_us: Fraction
    full_horizon_wire_fraction: Fraction
    full_horizon_reserved_fraction: Fraction
    traffic_window_wire_fraction: Fraction | None
    traffic_window_reserved_fraction: Fraction | None
    total_generated_entries: int
    total_accepted_entries: int
    total_backpressured_entries: int
    total_transmitted_stream_entries: int
    total_starvation_events: int
    total_idle_events: int
    total_deadline_misses: int
    final_outstanding_entries: int
    service_count_span: int
    transactions: tuple["TransactionRecord", ...]
    terminal_inflight_transaction: "TransactionRecord | None"
    anomalies: tuple[str, ...]


@dataclass(frozen=True)
class ReferenceVector:
    name: str
    external_channels: int
    uart_baud: int
    host_bus_bps: int


@dataclass
class _ChannelState:
    host_queue: Deque[Fraction]
    fifo: Deque[Fraction | None]
    next_arrival_us: Fraction | None
    next_drain_us: Fraction | None
    request_ready_us: Fraction | None = None
    reserved_fifo_entries: int = 0
    generated_entries: int = 0
    accepted_entries: int = 0
    backpressured_entries: int = 0
    serializer_slots_filled: int = 0
    transmitted_stream_entries: int = 0
    starvation_events: int = 0
    idle_events: int = 0
    queue_high_water_entries: int = 0
    minimum_fifo_entries: int = 0
    bus_transactions: int = 0
    bus_payload_entries: int = 0
    max_latency_us: Fraction | None = None
    completed_deadline_misses: int = 0
    last_service_completion_us: Fraction | None = None
    max_service_gap_us: Fraction | None = None


@dataclass(frozen=True)
class _Transaction:
    channel: int
    payload: tuple[Fraction, ...]
    started_us: Fraction
    completes_us: Fraction


@dataclass(frozen=True)
class TransactionRecord:
    channel: int
    payload_entries: int
    started_us: Fraction
    completes_us: Fraction
    fifo_entries_at_start: int
    reserved_entries_at_start: int


REFERENCE_VECTORS = (
    ReferenceVector("MIRRORED_5_I2C_1M_115200", 3, 115_200, 1_000_000),
    ReferenceVector("INDEPENDENT_10_I2C_1M_115200", 8, 115_200, 1_000_000),
    ReferenceVector("INDEPENDENT_10_SPI_4M_115200", 8, 115_200, 4_000_000),
    ReferenceVector("INDEPENDENT_10_SPI_15M_1M", 8, 1_000_000, 15_000_000),
    ReferenceVector("INDEPENDENT_10_SPI_26M_1M", 8, 1_000_000, 26_000_000),
    ReferenceVector("ALL_EXTERNAL_10_SPI_4M_115200", 10, 115_200, 4_000_000),
    ReferenceVector("ALL_EXTERNAL_10_SPI_26M_1M", 10, 1_000_000, 26_000_000),
)


def fifo_full_time_us(
    fifo_entries: int,
    frame_bits: int,
    baud: int,
) -> Fraction:
    if fifo_entries < 1 or frame_bits < 1 or baud < 1:
        raise ValueError("FIFO entries, frame bits and baud must be positive")
    return Fraction(fifo_entries * frame_bits * 1_000_000, baud)


def static_planning_screen(config: SchedulerConfig) -> StaticPlanningScreen:
    """Apply the legacy TX-only analytical screen from the v1.2 plan.

    This deliberately excludes transaction gaps and all IRQ, status, RX,
    retry and fault traffic.  A candidate verdict is never a capacity PASS.
    """
    required_bus_bps = Fraction(
        config.external_channels
        * config.uart_baud
        * config.duty_numerator
        * config.host_bits_per_uart_entry
        * config.guard_numerator,
        config.frame_bits
        * config.duty_denominator
        * config.guard_denominator,
    )
    utilization = required_bus_bps / config.host_bus_bps
    verdict = (
        PlanningVerdict.SCREEN_CANDIDATE
        if utilization <= config.planning_limit
        else PlanningVerdict.SCREEN_REJECT
    )
    return StaticPlanningScreen(
        required_bus_bps=required_bus_bps,
        utilization=utilization,
        planning_limit=config.planning_limit,
        fifo_full_time_us=fifo_full_time_us(
            config.fifo_capacity_entries,
            config.frame_bits,
            config.uart_baud,
        ),
        verdict=verdict,
    )


def reference_static_screens() -> dict[str, StaticPlanningScreen]:
    return {
        vector.name: static_planning_screen(
            SchedulerConfig(
                external_channels=vector.external_channels,
                uart_baud=vector.uart_baud,
                host_bus_bps=vector.host_bus_bps,
            )
        )
        for vector in REFERENCE_VECTORS
    }


def _refresh_requests(
    states: list[_ChannelState],
    config: SchedulerConfig,
    now_us: Fraction,
) -> None:
    trigger = config.refill_trigger_free_entries
    assert trigger is not None
    for state in states:
        free_entries = (
            config.fifo_capacity_entries
            - len(state.fifo)
            - state.reserved_fifo_entries
        )
        if state.host_queue and free_entries >= trigger:
            if state.request_ready_us is None:
                state.request_ready_us = now_us + config.irq_service_latency_us
        elif not state.host_queue:
            state.request_ready_us = None


def _select_ready_channel(
    states: list[_ChannelState],
    cursor: int,
    now_us: Fraction,
) -> int | None:
    for offset in range(len(states)):
        channel = (cursor + offset) % len(states)
        ready = states[channel].request_ready_us
        if ready is not None and ready <= now_us:
            return channel
    return None


def simulate(config: SchedulerConfig) -> SchedulerRun:
    """Run a deterministic finite-horizon sensitivity simulation."""

    arrival_period_us = config.source_arrival_period_us
    serializer_period_us = config.serializer_character_period_us
    initial_fill = config.initial_fifo_fill_entries
    arrival_stop_us = config.arrival_stop_us
    assert initial_fill is not None
    assert arrival_stop_us is not None
    arrival_stop = Fraction(arrival_stop_us, 1)
    first_arrival = (
        arrival_period_us if arrival_period_us <= arrival_stop else None
    )
    states = [
        _ChannelState(
            host_queue=deque(),
            fifo=deque([None] * initial_fill),
            next_arrival_us=first_arrival,
            next_drain_us=serializer_period_us if initial_fill else None,
            minimum_fifo_entries=initial_fill,
        )
        for _ in range(config.external_channels)
    ]

    horizon_us = Fraction(config.horizon_us, 1)
    now_us = Fraction(0, 1)
    bus_ready_us = Fraction(0, 1)
    bus_wire_busy_us = Fraction(0, 1)
    bus_gap_reserved_us = Fraction(0, 1)
    traffic_window_wire_busy_us = Fraction(0, 1)
    traffic_window_gap_reserved_us = Fraction(0, 1)
    active: _Transaction | None = None
    transaction_records: list[TransactionRecord] = []
    round_robin_cursor = 0

    while now_us <= horizon_us:
        # Conservative tie policy: new demand is made visible first, then an
        # existing UART completion is evaluated before a host-bus write that
        # completes at exactly the same timestamp.
        for state in states:
            if state.next_arrival_us is not None and state.next_arrival_us == now_us:
                state.generated_entries += 1
                if (
                    config.host_queue_limit_entries is not None
                    and len(state.host_queue) >= config.host_queue_limit_entries
                ):
                    state.backpressured_entries += 1
                else:
                    state.host_queue.append(now_us)
                    state.accepted_entries += 1
                    state.queue_high_water_entries = max(
                        state.queue_high_water_entries,
                        len(state.host_queue),
                    )
                next_arrival = now_us + arrival_period_us
                state.next_arrival_us = (
                    next_arrival if next_arrival <= arrival_stop else None
                )

        for channel, state in enumerate(states):
            if state.next_drain_us is not None and state.next_drain_us == now_us:
                if state.fifo:
                    token = state.fifo.popleft()
                    state.serializer_slots_filled += 1
                    if token is not None:
                        state.transmitted_stream_entries += 1
                        latency = now_us - token
                        if state.max_latency_us is None or latency > state.max_latency_us:
                            state.max_latency_us = latency
                        if config.deadline_us is not None and latency > config.deadline_us:
                            state.completed_deadline_misses += 1
                    if state.fifo:
                        state.next_drain_us += serializer_period_us
                    else:
                        demand_pending = bool(state.host_queue) or (
                            active is not None
                            and active.channel == channel
                            and bool(active.payload)
                        )
                        if demand_pending:
                            state.starvation_events += 1
                        else:
                            state.idle_events += 1
                        state.next_drain_us = None
                else:
                    raise AssertionError("serializer completion had no FIFO entry")
                state.minimum_fifo_entries = min(
                    state.minimum_fifo_entries,
                    len(state.fifo),
                )

        if active is not None and active.completes_us == now_us:
            state = states[active.channel]
            state.reserved_fifo_entries -= len(active.payload)
            if state.reserved_fifo_entries < 0:
                raise AssertionError("FIFO reservation became negative")
            serializer_was_idle = state.next_drain_us is None
            state.fifo.extend(active.payload)
            if (
                len(state.fifo) + state.reserved_fifo_entries
                > config.fifo_capacity_entries
            ):
                raise AssertionError("modeled transaction overflowed a FIFO")
            if serializer_was_idle:
                state.next_drain_us = now_us + serializer_period_us
            state.bus_transactions += 1
            state.bus_payload_entries += len(active.payload)
            if state.last_service_completion_us is not None:
                service_gap = now_us - state.last_service_completion_us
                if (
                    state.max_service_gap_us is None
                    or service_gap > state.max_service_gap_us
                ):
                    state.max_service_gap_us = service_gap
            state.last_service_completion_us = now_us
            wire_duration = active.completes_us - active.started_us
            bus_wire_busy_us += wire_duration
            traffic_wire_end = min(active.completes_us, arrival_stop)
            if traffic_wire_end > active.started_us:
                traffic_window_wire_busy_us += (
                    traffic_wire_end - active.started_us
                )
            bus_ready_us = now_us + config.inter_transaction_gap_us
            gap_end = min(bus_ready_us, horizon_us)
            if gap_end > now_us:
                bus_gap_reserved_us += gap_end - now_us
            traffic_gap_end = min(gap_end, arrival_stop)
            if traffic_gap_end > now_us:
                traffic_window_gap_reserved_us += traffic_gap_end - now_us
            active = None

        _refresh_requests(states, config, now_us)

        if active is None and bus_ready_us <= now_us and now_us < horizon_us:
            selected = _select_ready_channel(states, round_robin_cursor, now_us)
            if selected is not None:
                state = states[selected]
                free_entries = (
                    config.fifo_capacity_entries
                    - len(state.fifo)
                    - state.reserved_fifo_entries
                )
                payload_entries = min(
                    config.burst_entries,
                    free_entries,
                    len(state.host_queue),
                )
                if payload_entries > 0:
                    duration = config.transaction_time_us(payload_entries)
                    completes = now_us + duration
                    payload = tuple(
                        state.host_queue.popleft()
                        for _ in range(payload_entries)
                    )
                    state.reserved_fifo_entries += len(payload)
                    if (
                        len(state.fifo) + state.reserved_fifo_entries
                        > config.fifo_capacity_entries
                    ):
                        raise AssertionError("FIFO reservation exceeded capacity")
                    active = _Transaction(
                        channel=selected,
                        payload=payload,
                        started_us=now_us,
                        completes_us=completes,
                    )
                    transaction_records.append(
                        TransactionRecord(
                            channel=selected,
                            payload_entries=len(payload),
                            started_us=now_us,
                            completes_us=completes,
                            fifo_entries_at_start=len(state.fifo),
                            reserved_entries_at_start=(
                                state.reserved_fifo_entries
                            ),
                        )
                    )
                    state.request_ready_us = None
                    round_robin_cursor = (selected + 1) % len(states)

        if now_us == horizon_us:
            break

        candidates: list[Fraction] = []
        if active is not None and active.completes_us > now_us:
            candidates.append(active.completes_us)
        for state in states:
            if state.next_arrival_us is not None and state.next_arrival_us > now_us:
                candidates.append(state.next_arrival_us)
            if state.next_drain_us is not None and state.next_drain_us > now_us:
                candidates.append(state.next_drain_us)
            if state.request_ready_us is not None and state.request_ready_us > now_us:
                candidates.append(state.request_ready_us)
        if active is None and bus_ready_us > now_us:
            candidates.append(bus_ready_us)
        candidates.append(horizon_us)
        future = [candidate for candidate in candidates if candidate > now_us]
        if not future:
            break
        now_us = min(future)

    if active is not None:
        partial_wire_end = min(horizon_us, active.completes_us)
        if partial_wire_end > active.started_us:
            bus_wire_busy_us += partial_wire_end - active.started_us
        partial_traffic_end = min(
            horizon_us,
            arrival_stop,
            active.completes_us,
        )
        if partial_traffic_end > active.started_us:
            traffic_window_wire_busy_us += (
                partial_traffic_end - active.started_us
            )

    active_payloads: dict[int, tuple[Fraction, ...]] = {}
    if active is not None:
        active_payloads[active.channel] = active.payload

    channel_results_list: list[ChannelResult] = []
    for index, state in enumerate(states):
        stream_fifo_timestamps = tuple(
            token for token in state.fifo if token is not None
        )
        outstanding_timestamps = (
            tuple(state.host_queue)
            + stream_fifo_timestamps
            + active_payloads.get(index, ())
        )
        final_outstanding = len(outstanding_timestamps)
        if (
            state.accepted_entries
            != state.transmitted_stream_entries + final_outstanding
        ):
            raise AssertionError("accepted-entry conservation failed")
        if (
            state.generated_entries
            != state.accepted_entries + state.backpressured_entries
        ):
            raise AssertionError("generated-entry conservation failed")
        if (
            len(state.fifo) + state.reserved_fifo_entries
            > config.fifo_capacity_entries
        ):
            raise AssertionError("terminal FIFO invariant failed")
        overdue = 0
        if config.deadline_us is not None:
            overdue = sum(
                1
                for timestamp in outstanding_timestamps
                if horizon_us - timestamp > config.deadline_us
            )
        channel_results_list.append(
            ChannelResult(
                channel=index,
                generated_entries=state.generated_entries,
                accepted_entries=state.accepted_entries,
                backpressured_entries=state.backpressured_entries,
                serializer_slots_filled=state.serializer_slots_filled,
                transmitted_stream_entries=state.transmitted_stream_entries,
                starvation_events=state.starvation_events,
                idle_events=state.idle_events,
                final_host_queue_entries=len(state.host_queue),
                queue_high_water_entries=state.queue_high_water_entries,
                final_fifo_entries=len(state.fifo),
                final_reserved_fifo_entries=state.reserved_fifo_entries,
                final_stream_fifo_entries=len(stream_fifo_timestamps),
                final_outstanding_entries=final_outstanding,
                minimum_fifo_entries=state.minimum_fifo_entries,
                bus_transactions=state.bus_transactions,
                bus_payload_entries=state.bus_payload_entries,
                max_latency_us=state.max_latency_us,
                completed_deadline_misses=state.completed_deadline_misses,
                overdue_outstanding_entries=overdue,
                max_service_gap_us=state.max_service_gap_us,
            )
        )
    channel_results = tuple(channel_results_list)

    screen = static_planning_screen(config)
    total_generated = sum(item.generated_entries for item in channel_results)
    total_accepted = sum(item.accepted_entries for item in channel_results)
    total_backpressured = sum(
        item.backpressured_entries for item in channel_results
    )
    total_transmitted_stream = sum(
        item.transmitted_stream_entries for item in channel_results
    )
    total_starvation = sum(item.starvation_events for item in channel_results)
    total_idle = sum(item.idle_events for item in channel_results)
    total_deadline_misses = sum(
        item.completed_deadline_misses + item.overdue_outstanding_entries
        for item in channel_results
    )
    final_outstanding = sum(
        item.final_outstanding_entries for item in channel_results
    )
    service_counts = [item.bus_transactions for item in channel_results]
    service_count_span = max(service_counts) - min(service_counts)

    anomalies: list[str] = []
    for item in channel_results:
        if item.starvation_events:
            anomalies.append(
                f"FIFO_STARVATION_WITH_PENDING_DEMAND:channel={item.channel}:"
                f"count={item.starvation_events}"
            )
        if item.backpressured_entries:
            anomalies.append(
                f"BACKPRESSURE:channel={item.channel}:"
                f"count={item.backpressured_entries}"
            )
        deadline_violations = (
            item.completed_deadline_misses + item.overdue_outstanding_entries
        )
        if deadline_violations:
            anomalies.append(
                f"DEADLINE_MISS_OR_OVERDUE:channel={item.channel}:"
                f"count={deadline_violations}"
            )
        if item.final_outstanding_entries:
            anomalies.append(
                f"FINAL_OUTSTANDING:channel={item.channel}:"
                f"entries={item.final_outstanding_entries}"
            )
    if active is not None:
        anomalies.append(
            f"TERMINAL_INFLIGHT:channel={active.channel}:"
            f"entries={len(active.payload)}:completes_us={active.completes_us}"
        )
    if screen.verdict == PlanningVerdict.SCREEN_REJECT:
        anomalies.append(
            "PLANNING_LIMIT_EXCEEDED:"
            f"utilization={screen.utilization}:limit={screen.planning_limit}"
        )

    if total_generated == 0:
        status = RunStatus.MODEL_NOT_RUN_EMPTY_VECTOR
    elif total_starvation or total_backpressured or total_deadline_misses:
        status = RunStatus.MODEL_VIOLATION_VISIBLE
    elif final_outstanding:
        status = RunStatus.MODEL_BACKLOG_PRESENT
    elif screen.verdict == PlanningVerdict.SCREEN_REJECT:
        status = RunStatus.MODEL_SCREEN_REJECTED_NO_VISIBLE_LOSS_YET
    else:
        status = RunStatus.MODEL_NO_VISIBLE_LOSS

    if config.deadline_us is None:
        deadline_status = DeadlineStatus.UNKNOWN_NO_THRESHOLD
    elif total_accepted == 0:
        deadline_status = DeadlineStatus.UNKNOWN_NO_SAMPLES
    elif total_deadline_misses:
        deadline_status = DeadlineStatus.MODEL_THRESHOLD_MISS
    elif total_backpressured or total_starvation:
        deadline_status = DeadlineStatus.UNKNOWN_TRAFFIC_VIOLATION
    elif final_outstanding:
        deadline_status = DeadlineStatus.UNKNOWN_OUTSTANDING_ENTRIES
    else:
        deadline_status = DeadlineStatus.MODEL_WITHIN_CALLER_THRESHOLD

    if arrival_stop > 0:
        traffic_window_wire_fraction: Fraction | None = (
            traffic_window_wire_busy_us / arrival_stop
        )
        traffic_window_reserved_fraction: Fraction | None = (
            (
                traffic_window_wire_busy_us
                + traffic_window_gap_reserved_us
            )
            / arrival_stop
        )
    else:
        traffic_window_wire_fraction = None
        traffic_window_reserved_fraction = None

    return SchedulerRun(
        config=config,
        screen=screen,
        status=status,
        deadline_status=deadline_status,
        channels=channel_results,
        bus_wire_busy_us=bus_wire_busy_us,
        bus_gap_reserved_us=bus_gap_reserved_us,
        traffic_window_wire_busy_us=traffic_window_wire_busy_us,
        traffic_window_gap_reserved_us=traffic_window_gap_reserved_us,
        full_horizon_wire_fraction=bus_wire_busy_us / horizon_us,
        full_horizon_reserved_fraction=(
            (bus_wire_busy_us + bus_gap_reserved_us) / horizon_us
        ),
        traffic_window_wire_fraction=traffic_window_wire_fraction,
        traffic_window_reserved_fraction=traffic_window_reserved_fraction,
        total_generated_entries=total_generated,
        total_accepted_entries=total_accepted,
        total_backpressured_entries=total_backpressured,
        total_transmitted_stream_entries=total_transmitted_stream,
        total_starvation_events=total_starvation,
        total_idle_events=total_idle,
        total_deadline_misses=total_deadline_misses,
        final_outstanding_entries=final_outstanding,
        service_count_span=service_count_span,
        transactions=tuple(transaction_records),
        terminal_inflight_transaction=(
            transaction_records[-1] if active is not None else None
        ),
        anomalies=tuple(anomalies),
    )


if __name__ == "__main__":
    for vector in REFERENCE_VECTORS:
        screen = reference_static_screens()[vector.name]
        print(
            "\t".join(
                (
                    vector.name,
                    f"required_bus_bps={float(screen.required_bus_bps):.3f}",
                    f"utilization={float(screen.utilization):.6f}",
                    f"verdict={screen.verdict.value}",
                )
            )
        )
