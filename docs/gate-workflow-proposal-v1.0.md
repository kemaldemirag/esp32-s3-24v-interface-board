# WF01 G0–G6 Gate Workflow Proposal

**Version:** 1.0.0-proposal

**Date:** 2026-10-21

**Status:** Recommendation for human review; not an approved project policy or gate decision.

## 1. Purpose and current baseline

This proposal defines a reviewable path from scope and requirements through architecture, design verification, manufacturing release, physical validation, and archival. It adapts staged exit criteria and independent design/verification tracking from open hardware and embedded-system projects; it does not copy their governance or claim that they use WF01 gates.

The current repository baseline remains unchanged: `G1-JOB01/REP01` and `G1-PORTFOLIO` are `BLOCKED`; schematic, PCB, BOM, and exact-part selection have not started. This proposal does not approve either G1 gate or authorize downstream design work. See the [project README](../README.md), [open decisions and closure record](open-decisions-v1.2.md), and [interface contract](interface-contract-v1.1.md).

## 2. Operating model

Track three things independently:

1. **Gate decision:** whether the defined exit criteria for a gate are met.
2. **Design maturity:** progress of the design artifacts for the current phase.
3. **Verification maturity:** progress and results of the corresponding review, analysis, or test plan.

Design completion, a test result, or a handoff must never silently become a gate PASS. A gate PASS requires all applicable mandatory exit criteria to have acceptable evidence and a recorded human approval.

Use the repository status vocabulary consistently:

- `PASS`: the stated criterion has sufficient evidence and is accepted within its stated scope.
- `BLOCKED`: a required dependency, evidence item, or decision remains open.
- `UNKNOWN`: an answer or value has not been established.
- `NOT_RUN`: an identified verification has not been executed.
- `N/A`: scope exclusion only; it is not a status.
- `TBD`: an unassigned role or identity only; it is not a status.

An `UNKNOWN` item is a gate blocker only when that gate's mandatory exit criterion depends on it. Preserve non-blocking unknowns as owned handoff items with a target gate; never invent values to remove a blocker.

## 3. Roles and approval

Record named people for the following roles before a gate review:

| Role | Responsibility |
|---|---|
| Gate Owner | Defines scope and exit criteria, convenes review, records the decision and unresolved items. |
| Evidence Owner | Collects and identifies source files, revisions, hashes, provenance, and evidence links. |
| Design Owner | Owns the design artifacts and technical decision records for the relevant gate. |
| Verification Owner | Defines verification methods and acceptance criteria; records execution and findings. |
| Independent Reviewer | Checks decisive evidence and calculations independently of their producer. |
| Approver | Human authority who accepts or rejects the gate decision within the stated scope. |

One person may fill multiple roles only when the project records that assignment; the independent review must remain independent of the evidence being checked. The AI assistant is not a project approver.

## 4. Gate workflow

| Gate | Objective and work | Minimum exit evidence | Decision / handoff |
|---|---|---|---|
| **G0 — Prepare** | Establish scope, work/package boundaries, named owners and approver, source of truth, decision log, requirement traceability, evidence register, and verification matrix. | Gate scope and owner assignments recorded; each requirement has an identifier, source or explicit unknown, evidence class, and accountable next action. | Gate Owner confirms the review baseline is ready. G0 readiness is not technical approval. |
| **G1 — Requirements and evidence closure** | Evaluate `G1-JOB01/REP01` and `G1-PORTFOLIO` separately. For JOB-01, independently verify PKG-A/B source bytes, SHA-256, license, provenance, and duplicate/conflict review. Obtain authorized customer/product-owner evidence for CF-09: `MIRRORED_5` or `INDEPENDENT_10`; if mirrored, resolve the required logical-to-physical mapping. Keep PKG-C out of the JOB-01 gate but in the portfolio assessment. | Applicable requirements and evidence have traceable sources, named owners, and reviewed closure records. CF-09's authoritative answer and any required mapping are explicit. Portfolio evidence covers PKG-A/B/C and JOB-01…05. | Approve each G1 gate independently. G1 PASS does not itself select serial topology, pins, or exact parts; those decisions belong to G2 after their requirements and criteria are resolved. Current G1 results remain `BLOCKED`. |
| **G2 — Technical architecture** | Turn accepted requirements into sourced numeric criteria. Resolve input profiles and protection approach, rail/current and thermal budgets, exact parts, serial/resource allocation and timing, interfaces, and pin freeze. Record alternatives, evidence, assumptions, unknowns, rationale, and consequences in decision records. Give no capacity credit to research-only options; define prototype evidence where an option requires validation. | Source-backed criteria, calculations, part decisions, interface/resource mapping, and pin decisions are reviewed and traceable to requirements. Required validation plans have owners and thresholds. | Approve architecture only against documented criteria. G1 handoff is not G2 PASS; G2 approval does not mean the schematic, PCB, or hardware has passed verification. |
| **G3 — Design and design verification** | Create the schematic only after required G1/G2 approvals. Perform ERC, model provenance review, applicable simulation, and pre-layout review. Bind boot/reset/brownout behavior, interface operation, power-off/back-power, and fault recovery checks to verification records. | Required schematic and analysis artifacts identify their revisions and tools; no unexplained blocking findings remain; models and decisive claims have provenance; verification results include method and disposition. | Approve the reviewed design baseline for layout. Open or failed checks remain visible and block affected exit criteria. |
| **G4 — Manufacturing design** | Complete PCB layout, DRC, BOM, manufacturing outputs, and design-for-manufacturing review. Keep outputs tied to the exact source/design revision. | Reviewed layout and BOM; KiCad/tool version and raw ERC/DRC outputs; classified and dispositioned findings; required production outputs; revision and integrity identifiers. | Approve a reproducible manufacturing package. This is not physical validation or compliance approval. |
| **G5 — Physical validation** | First record whether bench testing applies to JOB-01 and which tests are in scope. For each applicable test, document requirement, DUT board revision, firmware commit/build identity where applicable, setup, equipment, procedure, acceptance threshold, result, and reviewer. | Approved test plan and scope decision; executed records linked to the tested DUT revision; measured evidence and findings dispositioned against stated criteria. Compliance testing requires an authorized standard and target, not an assumed level. | Approve only the tests actually executed within their recorded scope. Applicability may be `UNKNOWN`; tests not run remain `NOT_RUN`. |
| **G6 — Release and archive** | Assemble the released design, editable sources, manufacturing outputs, source/provenance records, hashes, test reports, known limitations, and reproducibility instructions. Record final human sign-off against one identified revision. | Release manifest identifies the exact revision and deliverables; integrity/provenance and licensing are recorded; required gate decisions and limitations are included; sign-off is attributable to named people. | Release only the approved revision. A subsequent change is assessed for impact and re-enters the earliest affected gate(s); it does not inherit approval automatically. |

## 5. Gate decision record

Create one decision record per gate and per independently assessed scope. At minimum, record:

- gate ID, scope, design/release revision, and review date;
- entry conditions and each exit criterion;
- criterion status and rationale;
- links to source evidence, raw verification outputs, and decision records;
- open blockers, non-blocking unknowns, owner, target gate, and next action;
- Gate Owner, Evidence Owner, Design Owner, Verification Owner, independent reviewer, and approver;
- approval outcome, limitations, and any dissent or conditional action.

The decision record must distinguish source facts, derived claims, engineering rules, assumptions, conflicts, and unknowns. Preserve raw evidence alongside summaries where applicable. The repository's [requirements traceability matrix](../requirements/requirements-traceability-v1.2.csv) and [closure/verification matrix](open-decisions-v1.2.md) are the starting records; keep their scope and statuses consistent with this proposal.

## 6. Change control and re-review

For any change to an approved requirement, source, architecture decision, design artifact, or test result:

1. Identify affected requirements, evidence, decisions, artifacts, and gates.
2. Record the change, rationale, owner, review, and new revision/hash where applicable.
3. Reopen the earliest affected gate and every downstream criterion whose evidence or assumptions are invalidated.
4. Retain prior approved records for auditability; do not overwrite historical evidence or carry forward a PASS without review.

Bug fixes may be handled within the current development phase only when the Gate Owner records that they do not invalidate an earlier approval. Feature or scope changes require impact review and an appropriately scoped re-approval.

## 7. Immediate next actions

These actions reflect the current closure record; this proposal does not perform or approve them:

1. Assign named human Gate, Evidence, Verification, independent-review, and approval roles.
2. Complete and independently verify PKG-A/B source bytes, hashes, license, provenance, and duplicate/conflict evidence.
3. Obtain authoritative CF-09 branch evidence and, if `MIRRORED_5` is selected, the mapping decision.
4. Reassess both G1 scopes independently and record explicit human decisions.
5. Start G2 only after the applicable G1 approval; leave later-gate unknowns owned and visible rather than guessing values.

Until the relevant G1 blockers are closed and approved, do not record G1 PASS, freeze pins, select exact parts, or begin schematic/PCB design. This is consistent with the current [project baseline](../README.md) and [G1 closure record](open-decisions-v1.2.md).

## 8. Research basis

These sources informed the proposal; they are references, not evidence that those projects use WF01 gates:

- [OpenTitan hardware development stages](https://github.com/lowRISC/opentitan/blob/master/doc/project_governance/development_stages.md) and [milestone definitions](https://github.com/lowRISC/opentitan/blob/master/doc/project_governance/project_milestone_definitions.md): staged maturity, exit criteria, distinct design and verification progress, and re-entry after changes.
- [OpenTitan RFC process](https://github.com/lowRISC/opentitan/blob/master/doc/project_governance/rfc_process.md): public technical proposal review and recorded committee decisions.
- [LibreSolar BMS C1 README](https://github.com/LibreSolar/bms-c1/blob/main/README.md) and [v0.3 test report](https://github.com/LibreSolar/bms-c1/blob/main/testing/v0.3/README.md): tested-prototype status and test evidence tied to hardware revision, firmware commit, setup, equipment, requirement, method, and result.
- [LibreSolar BMS firmware README](https://github.com/LibreSolar/bms-firmware): release decisions follow significant updates and testing across multiple boards.
- [eumesmo 24 V ESP32 I/O board](https://github.com/thermseekr/eumesmo-io-24v16ch): revision history records hardware changes and engineering rationale, including power sequencing, EMC/layout, and test-point changes.
