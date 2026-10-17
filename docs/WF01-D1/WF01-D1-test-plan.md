# WF01-D1 Test Plan

**Product:** ESP32-S3 Interface Architecture Decision Workbench  
**Stage:** WF01-D1  
**Plan version:** 1.0.0-draft  
**Execution status:** All tests are `NOT_RUN`. D1 defines tests; it does not implement or execute the engine.

## 1. Test objective and claim limit

This plan verifies the product contract, not a PCB. It covers schema behavior, eight initial rules, four MVP scenarios, evidence gating, deterministic execution, result aggregation, and the REP-02 integration boundary. Synthetic fixture values are test data only and must not be copied into a customer configuration as requirements.

A test-level `PASS` in D2 will mean only that the software produced the expected result for the stated fixture. It will not mean G1, the board, a component, or a compliance target has passed.

## 2. Test harness requirements for D2

- Every fixture must declare `contract_version`, `configuration_id`, registry version, scenario identifiers, and a complete evidence catalog.
- Unknown customer numbers and Booleans must use `state: UNKNOWN` and `value: null`; a fixture must never substitute zero or `false` for unknown.
- Tests must run offline against pinned local inputs. Network, current time, absolute paths, locale, and iteration order must not influence technical results.
- The harness must preserve the raw input, canonical normalized input, input SHA-256, registry SHA-256, scenario-matrix SHA-256, ordered results, and engine version.
- Positive and negative fixtures may use synthetic values only when explicitly marked `SYNTHETIC_TEST_DATA`.
- Expected result matching must compare status, rule ID, reason code, decisive inputs, decisive evidence, pass scope, limitations, and next action.
- The initial execution record for every test in this document is `NOT_RUN`.

## 3. Rule tests

| Test ID | Rule | Fixture and decisive input | Expected rule result | Expected rationale / assertion | Initial execution |
|---|---|---|---|---|---|
| `TC-GPIO-001-POS-01` | RULE-GPIO-001 | Internal USB is known and applicable; D- is GPIO19; D+ is GPIO20; both are exclusively reserved for USB | `PASS` | Exact 19/20 mapping and exclusive reservations satisfy only this rule; pass scope is `RULE_ONLY` | `NOT_RUN` |
| `TC-GPIO-001-NEG-01` | RULE-GPIO-001 | Internal USB pins are known, but GPIO20 is also assigned to an unrelated TFT signal | `FAIL` | A complete known assignment conflicts with the USB D+ reservation | `NOT_RUN` |
| `TC-GPIO-001-MISS-01` | RULE-GPIO-001 | Internal USB Host is known, but the reservation or assignment record for GPIO19/20 is absent or `UNKNOWN` | `BLOCKED` | Applicability is known; decisive reservation evidence is incomplete | `NOT_RUN` |
| `TC-GPIO-002-POS-01` | RULE-GPIO-002 | N8 profile is evidenced; GPIO45 boot drive and bias are known and are not high or high-biased | `PASS` | The fixture demonstrates only the no-boot-high contract | `NOT_RUN` |
| `TC-GPIO-002-NEG-01` | RULE-GPIO-002 | N8 profile is evidenced; GPIO45 is known to be driven HIGH or pulled up in the boot window | `FAIL` | Known boot-high behavior violates the rule | `NOT_RUN` |
| `TC-GPIO-002-MISS-01` | RULE-GPIO-002 | N8 profile is known, but GPIO45 boot behavior is `UNKNOWN` | `BLOCKED` | Applicability is known but the decisive behavior is not | `NOT_RUN` |
| `TC-GPIO-003-POS-01` | RULE-GPIO-003 | ESP32-S3 applies; GPIO3 has a known deterministic boot state and known non-floating external bias | `PASS` | GPIO3 is not left high-impedance or uncontrolled | `NOT_RUN` |
| `TC-GPIO-003-NEG-01` | RULE-GPIO-003 | GPIO3 boot drive is `HIGH_Z` or external bias is `NONE`/`FLOATING` | `FAIL` | Complete known data demonstrates an unsafe floating strap state | `NOT_RUN` |
| `TC-GPIO-003-MISS-01` | RULE-GPIO-003 | ESP32-S3 applies, but GPIO3 boot drive or bias is `UNKNOWN` | `BLOCKED` | The strap contract cannot close without the missing data | `NOT_RUN` |
| `TC-USB-001-POS-01` | RULE-USB-001 | Internal USB Host is selected and USB-Serial/JTAG is explicitly non-concurrent | `PASS` | Only one internal-PHY owner is active at a time | `NOT_RUN` |
| `TC-USB-001-NEG-01` | RULE-USB-001 | Internal USB Host and USB-Serial/JTAG are both explicitly required concurrently | `FAIL` | Two known concurrent owners claim the internal PHY | `NOT_RUN` |
| `TC-USB-001-MISS-01` | RULE-USB-001 | Internal Host is known; simultaneous debug requirement is `UNKNOWN` | `BLOCKED` | The PHY ownership policy is unresolved | `NOT_RUN` |
| `TC-USB-002-POS-01` | RULE-USB-002 | Internal Host applies; UART0 is an explicit recovery transport; TX/RX are GPIO43/44 and exclusively reserved | `PASS` | The defined recovery-path contract is satisfied | `NOT_RUN` |
| `TC-USB-002-NEG-01` | RULE-USB-002 | Internal Host applies; UART0 is omitted, mapped to different known pins, or conflicts on GPIO43/44 | `FAIL` | A complete known configuration removes or conflicts with the required recovery path | `NOT_RUN` |
| `TC-USB-002-MISS-01` | RULE-USB-002 | Internal Host applies; UART0 recovery policy or one UART0 pin is `UNKNOWN` | `BLOCKED` | Applicability is known; recovery evidence is incomplete | `NOT_RUN` |
| `TC-SERIAL-001-POS-01` | RULE-SERIAL-001 | Synthetic demand is two independent TX streams; verified native/validated alternative capacity totals two or more after reservations | `PASS` | Verified capacity is at least the known demand; research-only resources are excluded | `NOT_RUN` |
| `TC-SERIAL-001-NEG-01` | RULE-SERIAL-001 | `SCN-SER-003` profile: demand is ten; every counted resource is known; verified capacity is lower than ten | `FAIL` | A known capacity deficit is demonstrated | `NOT_RUN` |
| `TC-SERIAL-001-MISS-01` | RULE-SERIAL-001 | Ten independent TX streams are required, but proposed external capacity is `UNKNOWN` or `RESEARCH_ONLY` | `BLOCKED` | Unvalidated or unknown capacity cannot support PASS | `NOT_RUN` |
| `TC-TFT-001-POS-01` | RULE-TFT-001 | Synthetic fixture: TFT is PRESENT or DNP_FUTURE; interface, decision reference, signal set, pin count, and conflict-free assignments are all known and consistent | `PASS` | Completeness and internal consistency satisfy this rule only | `NOT_RUN` |
| `TC-TFT-001-NEG-01` | RULE-TFT-001 | TFT interface and count are known, but required unique signals do not match the count or a pin conflicts | `FAIL` | Complete known TFT data is contradictory | `NOT_RUN` |
| `TC-TFT-001-MISS-01` | RULE-TFT-001 | TFT is PRESENT or DNP_FUTURE, but Q-C11/interface/pin demand is `UNKNOWN` | `BLOCKED` | The interface architecture cannot close; SPI is not inferred | `NOT_RUN` |
| `TC-EVIDENCE-001-POS-01` | RULE-EVIDENCE-001 | Candidate result is `PASS`; every decisive reference resolves to local hash-verified, policy-sufficient evidence; derived claims have parent links | `PASS` | Evidence gate permits the unchanged `RULE_ONLY` PASS | `NOT_RUN` |
| `TC-EVIDENCE-001-NEG-01` | RULE-EVIDENCE-001 | Candidate PASS is contradicted by resolved evidence or claims `PRODUCT`/`G1` scope | `FAIL` | Contradiction or pass-scope escalation is explicit | `NOT_RUN` |
| `TC-EVIDENCE-001-MISS-01` | RULE-EVIDENCE-001 | Candidate PASS depends on a missing, unresolved, hash-unverified, or policy-insufficient decisive source | `BLOCKED` | Missing evidence is never converted to PASS | `NOT_RUN` |

## 4. MVP scenario acceptance tests

| Scenario | Required rule and scenario assertions | Expected scenario result for the defining negative fixture | Initial execution |
|---|---|---|---|
| `SCN-USB-003` — USB PHY conflict | Internal PHY, Host/OTG, USB-Serial/JTAG, and concurrency are known; RULE-USB-001 emits the decisive result; evidence gate runs on any candidate PASS | `FAIL` when both internal-PHY owners are concurrent; `BLOCKED` if concurrency is unknown | `NOT_RUN` |
| `SCN-USB-002` — pad JTAG GPIO budget | The same source assumption profile must supply both demand 25 and available 24; GPIO39-42 reservation and all excluded/included categories are recorded | `FAIL` only for that known 25/24 profile; `BLOCKED` if CF-09, TFT/LCD demand, or supply profile is unresolved | `NOT_RUN` |
| `SCN-SER-003` — ten independent TX | Demand is exactly ten independent streams; capacity includes only native availability and validated, known alternatives | `FAIL` when verified capacity is known below ten; `BLOCKED` when proposed capacity is unknown or research-only | `NOT_RUN` |
| `SCN-TFT-005` — undefined TFT | Provisioning is PRESENT or DNP_FUTURE while Q-C11/interface/pin demand is unknown | `BLOCKED`; the engine must not select SPI or return PASS | `NOT_RUN` |

`SCN-USB-002` currently has no separate ninth rule in the mandated eight-rule registry. D2 must resolve open decision `OD-D1-008`: implement the 25/24 comparison as a typed scenario assertion or add a reviewed general GPIO-capacity rule in a future registry version. It must not hide the comparison in report code.

## 5. Cross-cutting contract tests

| Test ID | Area | Fixture / operation | Expected result | Initial execution |
|---|---|---|---|---|
| `TC-SCHEMA-001-INVALID-JSON` | Loader | Syntactically invalid JSON | Configuration validation `FAIL`; every rule and scenario `NOT_RUN` | `NOT_RUN` |
| `TC-SCHEMA-002-MISSING-REQUIRED` | Schema | Remove a required root property | Configuration validation `FAIL`; no repair or default insertion | `NOT_RUN` |
| `TC-SCHEMA-003-UNKNOWN-NUMBER` | Schema | Supply an unknown current or voltage as `state=UNKNOWN`, `value=null`, with reason and next action | Schema-valid; zero is not inserted; owning downstream check is `UNKNOWN` or `BLOCKED` | `NOT_RUN` |
| `TC-SCHEMA-004-DUPLICATE-ID` | Referential integrity | Duplicate an evidence or assignment identifier | Validation `FAIL`; affected rules `NOT_RUN` | `NOT_RUN` |
| `TC-SCHEMA-005-UNRESOLVED-REF` | Referential integrity | Reference an evidence ID absent from the catalog | Validation `FAIL` or candidate result `BLOCKED` according to phase; never PASS | `NOT_RUN` |
| `TC-DSL-001-UNKNOWN-OPERATOR` | Rule registry | Add an unrecognized condition operator | Registry loading `FAIL`; arbitrary execution is prohibited | `NOT_RUN` |
| `TC-DETERMINISM-001-REPEAT` | Determinism | Run the identical canonical configuration, registry, scenarios, and evidence snapshot twice | Same ordered technical results and same technical result SHA-256 | `NOT_RUN` |
| `TC-DETERMINISM-002-ORDER` | Determinism | Reorder semantically unordered evidence references and selected scenario IDs | Canonical input and technical result SHA-256 remain unchanged | `NOT_RUN` |
| `TC-DETERMINISM-003-EPHEMERAL` | Determinism | Change timestamp, absolute source path, or report formatting only | Technical result and hash remain unchanged; report metadata may differ | `NOT_RUN` |
| `TC-AGGREGATE-001-PRECEDENCE` | Scenario aggregation | Applicable results contain PASS, UNKNOWN, BLOCKED, and FAIL | Scenario result is `FAIL` according to declared precedence | `NOT_RUN` |
| `TC-AGGREGATE-002-NO-APPLICABLE` | Scenario aggregation | Scenario runs but no rule is applicable | Scenario result is `NOT_RUN`, not PASS | `NOT_RUN` |
| `TC-SCOPE-001-RULE-PASS` | Claim control | All applicable initial rules PASS | Output remains rule/scenario scoped; G1 and product state are unchanged | `NOT_RUN` |
| `TC-REP02-001-HASH-MISMATCH` | REP-02 adapter | Response `input_configuration_sha256` differs from request | Response rejected; no REP-01 result is upgraded | `NOT_RUN` |
| `TC-REP02-002-MISSING-PROVENANCE` | REP-02 adapter | Assessment cites a model without source, hash, license state, or validity bounds | Assessment evidence is `BLOCKED`; no physical PASS claim | `NOT_RUN` |
| `TC-REP02-003-PASS-SCOPE` | REP-02 adapter | REP-02 returns PASS for one rail check | Only that check remains PASS; G1 and REP-01 product approval remain unchanged | `NOT_RUN` |
| `TC-REP02-004-BOUNDARY` | Architecture | Add a rail-margin, regulator-stability, sequencing, or thermal calculator to the REP-01 engine | Architecture conformance `FAIL`; responsibility belongs to REP-02 | `NOT_RUN` |

## 6. Result comparison requirements

For every executed D2 test, the harness must assert:

1. exactly one registered `rule_id` for a rule result;
2. a valid `status` from `PASS`, `FAIL`, `UNKNOWN`, `BLOCKED`, `NOT_RUN`;
3. stable `reason_code` and a non-empty human-readable rationale;
4. the exact decisive input paths and resolved evidence IDs;
5. at least one limitation and a concrete next action for non-PASS results;
6. `pass_scope: RULE_ONLY` for PASS;
7. no unapproved mutation of the input fixture;
8. deterministic ordering by `scenario_id`, then `rule_id`.

## 7. D2 entry acceptance criteria

D2 test implementation may start only after this D1 contract set is reviewed and the following are accepted:

- the evidenced-value representation and no-default policy;
- the bounded `wf01-expression-v1` operator set;
- the eight rule semantics and status boundaries;
- the 31-source-row to 30-logical-scenario normalization;
- the treatment of `SCN-USB-002` in `OD-D1-008`;
- the REP-02 ownership boundary and request/response hash policy;
- the rule-only PASS scope.

Until then, all tests in this plan remain `NOT_RUN`.
