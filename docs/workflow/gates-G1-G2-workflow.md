# G1 → G2 transition plan

**Current state:** G1 is `BLOCKED`; no schematic or exact part is authorized. This plan defines a controlled handoff, not a gate decision.

## 1. Close the G1 questions

Record each answer and evidence in `Q-C-response-template.csv`. The criterion is evidence-backed closure or an explicitly approved `NOT_APPLICABLE`; proposed/reference values do not close a customer requirement.

| Question | Minimum closure evidence | G1 / G2 effect |
|---|---|---|
| Q-C01 | Approved continuous input min/max range, conditions, units, and source | Defines input operating envelope for REQ-001; required before protection-part screening |
| Q-C02 | Applicable suppression state and transient/load-dump/OEM profile, revision, and functional policy | Defines transient withstand/clamping requirement; no automotive profile inferred from a generic reference |
| Q-C03 | Reverse-polarity and jump-start voltage/duration and required behavior | Defines reverse/jump-start protection envelope |
| Q-C04 | Selected-device or generic-host scope, maximum USB current and inrush, operating conditions | Closes USB VBUS and 5 V rail demand inputs |
| Q-C05 | LCD controller/panel, logic voltage/interface, complete signals, and backlight current/conditions | Closes LCD interface and power/GPIO demand |
| Q-C06 | Ambient and enclosure temperature, cooling/airflow, and operating conditions | Closes thermal/derating boundary |
| Q-C07 | Explicit independent-vs-mirrored TX topology, stream count/concurrency, and approved frozen GPIO record | Resolves CF-09; no serializer or pin assignment before closure |
| Q-C08 | Baud/rate, cable length/capacitance, load, and concurrency requirements for RS-232 and TTL | Constrains transceiver/driver selection and verification |
| Q-C09 | Product domain, applicable ESD/EFT/surge profiles and levels, ports, and functional criteria | Sets protection and test acceptance criteria; no compliance claim by profile name alone |
| Q-C10 | Target manufacturer/assembly class and current stack-up, clearance, and process constraints | Feeds G2/G3 constraints; manufacturer assumptions must be tagged |
| Q-C11 | TFT/DNP decision and approved interface-specific signal list, touch, and PSRAM requirements | Closes IC-12 and GPIO/memory demand; no SPI or pin-count default |

Also close or disposition the non-question G1 blockers in `docs/open-decisions.md`: package integrity/licensing/provenance (PKG-A/B/C as applicable), CF-08 domain classification, D-JTAG, D-USB-EFUSE, D-EFUSE-IO35-37, D-ANTENNA, scope (Q-S04), gate ownership, and any source/traceability conflict. Record what is deferred, why it cannot invalidate G2, its owner, due date, and exit condition.

## 2. G1 decision and controlled handoff

1. Freeze the requirements, interface contract, response records, decision log, and evidence snapshot by revision/hash.
2. Complete `checklist-G1-closure.md`; unresolved `UNKNOWN`, `CONFLICT`, or `BLOCKED` items affecting the agreed scope prevent PASS.
3. Gate owner checks acceptance criteria and evidence quality; authorized approver records `PASS`, `BLOCKED`, or `FAIL` in `approval-record-template.csv`.
4. A G1 PASS only authorizes the bounded G2 planning/selection scope named in the approval. It is not approval of a part, schematic, PCB, safety, EMC, or product.
5. Carry every open item, risk, and evidence limitation into G2. Any changed customer input reopens affected decisions and invalidates stale calculations.

## 3. G2 exact-part selection procedure

Do not nominate an exact part while a required input envelope is unknown. For each function (input protection, conversion, VBUS switch, level translation, UART/RS-232, display/other loads):

1. Create a decision record tied to the requirement, interface, Q-C response, and acceptance criteria.
2. Translate the approved envelope into electrical and environmental constraints with units, tolerances, transients, loads, startup/fault behavior, and derating.
3. Identify candidate parts only from traceable manufacturer primary sources. Record source URL/document revision, locator, access date, and file hash where available.
4. Screen absolute maximums and recommended operating conditions separately; check ratings across the complete envelope, thermal/SOA behavior, power-off/backfeed, startup/fault response, package/footprint, availability/lifecycle, license/provenance, and manufacturing constraints as applicable.
5. Compare candidates in a selection matrix. A secondary source or similar board is a discovery aid, not proof that a part meets this project's requirements.
6. Calculate worst-case limits using evidenced values and datasheet tolerances. Record formulas, assumptions, units, derating, and independent review. Unknown inputs remain `BLOCKED`.
7. Approve one selected part or record why no candidate is acceptable. Update BOM, schematic symbol/footprint links, evidence catalog, and traceability together.
8. Re-run checks on the frozen schematic/BOM revision; substitutions require a new decision and impact review.

No manufacturer part number or numerical electrical limit is preselected by this workflow.

## 4. G2 schematic-entry checklist

Schematic entry starts only after explicit G2 authorization by the G2 owner/approver.

- [ ] Approved G1 record and scope; named G2 owner and reviewer.
- [ ] Controlled requirements, Q-C answers, interface contract, GPIO map, decision records, and evidence snapshot are revision-pinned.
- [ ] Power-input profile and protection policy are approved; every safety-/protection-critical part has a reviewed selection record.
- [ ] Rail budget has customer load evidence, worst-case/peak demand, margin policy, verified capacity, thermal/efficiency evidence, and reviewer sign-off. Missing inputs remain blocked.
- [ ] Serial topology, channel/concurrency requirements, transceiver/level-shifter decisions, and boot/power-off behavior are closed.
- [ ] USB PHY/debug/recovery and VBUS power/current-limit decisions are explicit; irreversible eFuse operation is separately controlled and is not part of schematic entry.
- [ ] LCD/TFT and GPIO/PSRAM reservations are either fully specified or formally excluded from this revision with an approved interface boundary.
- [ ] Exact parts have manufacturer datasheets, lifecycle/sourcing review, symbols/footprints, and traceable evidence.
- [ ] Protection, ESD, connector, test-point, and manufacturing/DFM requirements are defined or tracked as approved G2 actions.
- [ ] Schematic conventions, sheet/block ownership, revision scheme, and ERC acceptance policy are agreed.
- [ ] ERC and design review are planned with owners and acceptance thresholds; no result is represented as run before it is.

## 5. WF01-D1 → G2 mapping

| WF01-D1 output | G2 input / artifact | Required bridge |
|---|---|---|
| Configuration schema and evidence-linked values | Approved requirements/configuration snapshot | Validate IDs, states, evidence references, revision, and unresolved values |
| Rule registry and per-rule results | Architecture constraints in decision records and schematic review | Preserve `RULE_ONLY` scope; a rule PASS is not a gate PASS |
| Scenario matrix and four MVP fixtures | Applicable design/test cases | Select applicable cases; keep backlog/unknown cases visible |
| `docs/interface-contract.md` | Schematic block/interface definitions | Map IC-01…IC-15 to sheet, connector, net, protection, and evidence |
| `requirements/requirements-traceability.csv` | G2 calculation, part decision, schematic, and verification records | Extend trace IDs; do not replace customer source references |
| Q-C01–Q-C11 signed responses | Protection envelope, load budget, interface/GPIO, and DFM inputs | Carry response evidence and reviewer into every dependent artifact |
| Open decision/risk records | Approved deviations and G2 action register | Give every deferred item an owner, date, effect, and closure condition |

## 6. Evidence traceability and approvals

Use `evidence-traceability.csv` to connect each requirement/Q-C/decision to its G2 calculation or schematic artifact, verification method, evidence source, and owner. Evidence must identify the claim it supports and limitations; hashes establish file identity, not correctness.

Approval sequence:

1. Requirement/customer authority signs customer values and scope.
2. Evidence owner checks provenance, locator, revision/hash, and evidence class.
3. Relevant technical lead reviews calculations and part decisions.
4. Gate owner checks the closure checklist and unresolved-risk disposition.
5. Authorized stakeholder/approver records approval, conditions, and evidence snapshot.

Approvals are immutable records; corrections create a superseding record. A GitHub label, automated check, WF01-D1 result, or checkbox is not stakeholder sign-off.
