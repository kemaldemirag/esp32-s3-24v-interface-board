# G1 closure checklist

**Gate status:** `BLOCKED` until each applicable blocker below has evidence and an explicit gate-owner decision. Marking a checkbox is not a gate approval.

## 1. Governance and input integrity

- [ ] G1 scope (JOB-01 only or portfolio scope) is recorded; gate owner and independent approver are named.
- [ ] Applicable source package(s) are identified; package hashes, licenses, provenance, duplicate/conflict checks, and missing-source exceptions are recorded (PKG-A/B/C as applicable).
- [ ] Requirements and interface-contract revisions are frozen for this review; every decision links to a requirement and evidence locator.
- [ ] Customer answers are recorded in `Q-C-response-template.csv`; an answer is `KNOWN` only when its evidence and approver are present. `UNKNOWN`, `CONFLICT`, or unanswered blockers remain open.

## 2. Customer questions (canonical IDs)

- [ ] Q-C01: continuous 24 V input minimum/maximum operating range.
- [ ] Q-C02: suppression arrangement and applicable transient/load-dump/OEM profile.
- [ ] Q-C03: reverse-polarity and jump-start voltage/duration profile.
- [ ] Q-C04: USB-A host load scope, maximum current, and inrush.
- [ ] Q-C05: LCD logic voltage, interface/mode, backlight current, and required signal set.
- [ ] Q-C06: ambient/enclosure temperature and cooling conditions.
- [ ] Q-C07: whether five TTL TX and five RS-232 TX are independent streams or mirrored streams; frozen GPIO record supplied.
- [ ] Q-C08: RS-232 baud/cable/load and TTL load characteristics.
- [ ] Q-C09: applicable ESD/EFT/surge target levels and functional criteria.
- [ ] Q-C10: target manufacturer, stack-up/assembly constraints, and production class.
- [ ] Q-C11: future TFT connector need, interface, complete signal/pin list, touch, and PSRAM requirements.

Record a signed `NOT_APPLICABLE` disposition only when the gate owner accepts the rationale and evidence. A customer question is not closed by a proposed value, reference-board value, or research envelope.

## 3. Architecture and engineering blockers

- [ ] CF-09 is resolved by an approved Q-C07 response and frozen GPIO/topology record; serial demand, concurrency, and required capacity are explicit.
- [ ] CF-08/product domain (automotive vs. industrial, as applicable) and functional behavior are approved; incompatible standards envelopes are not mixed.
- [ ] D-JTAG and D-USB-EFUSE are explicit decisions with evidence; irreversible eFuse actions have the required separate approval. No eFuse action is authorized by this checklist.
- [ ] D-EFUSE-IO35-37, D-ANTENNA, and other interface-contract open decisions are closed or have an approved, bounded deferral that cannot invalidate G2 entry.
- [ ] Requirements traceability has no unexplained conflict or orphaned decision. All applicable requirement rows have an owner, acceptance criterion, and verification method.
- [ ] Exact-part constraints and evidence are sufficient to begin the controlled G2 selection process; any G1-required critical candidate is supported by primary datasheet, lifecycle/source, derating, and protection-profile evidence. A research recommendation is not a part selection.
- [ ] Risk register has no unaccepted high-severity risk that prevents safe schematic entry.

## 4. Evidence and approval

- [ ] Every closure claim has an evidence ID, stable source/locator, revision or hash where available, evidence class, and claim it supports.
- [ ] Evidence is adequate for the claim; secondary sources and reference designs are not presented as customer requirements or validation results.
- [ ] Gate-owner review and stakeholder sign-off are recorded in `approval-record-template.csv`.
- [ ] Open items are either closed with evidence or explicitly deferred by the authorized approver with owner, due date, impact, and exit condition.
- [ ] G1 decision is recorded as `PASS`, `BLOCKED`, or `FAIL` with rationale. `PASS` authorizes only the agreed G2 scope; it does not assert schematic, hardware, compliance, or product validation.

## Closure decision

| Field | Entry |
|---|---|
| Gate / scope | G1 / JOB-01 or approved scope |
| Decision | `BLOCKED` |
| Evidence snapshot / revision | `TBD` |
| Gate owner / date | `TBD` |
| Approver / date | `TBD` |
| Conditions / deferred items | `TBD` |
| G2 authorized scope | `NONE` until explicit approval |
