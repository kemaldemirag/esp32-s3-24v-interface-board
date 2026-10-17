# WF01-D1 Decision Engine Product Contract

**Product:** ESP32-S3 Interface Architecture Decision Workbench  
**Contract version:** 1.0.0-draft  
**Stage:** WF01-D1  
**Date:** 2026-10-17  
**Implementation status:** Specification only. Product code and hardware tests have not been started.

## 1. Purpose and decision boundary

The workbench accepts an evidence-linked ESP32-S3 interface configuration, validates its structure, evaluates registered architecture constraints, runs selected decision scenarios, and emits reproducible rule-scoped results. Its first release covers GPIO reservations, boot and strapping safety, USB PHY and recovery-path conflicts, serial-transmitter capacity, TFT definition completeness, and evidence sufficiency.

The workbench is a design-time decision aid. It does not perform electrical simulation, bench testing, ERC, DRC, exact-part qualification, automotive compliance assessment, or project-gate approval. A `PASS` means only that one registered rule was satisfied for the evaluated configuration and evidence snapshot. It never means G1, the board, or the product is approved.

The following are explicit non-goals for D1 and the first REP-01 MVP:

- KiCad schematic or PCB generation.
- A general rail-budget, regulator-derating, stability, sequencing, thermal, or load-step engine.
- Automatic assignment of values for missing customer parameters.
- Automatic eFuse programming recommendations or production provisioning.
- Inferring pin assignments, serializer capacity, current limits, thresholds, or part numbers from examples.
- Live REP-02 execution. D1 defines only a versioned file contract for later integration.

## 2. Normative terms and invariants

`MUST`, `MUST NOT`, `SHOULD`, and `MAY` are normative within this product contract.

1. Every customer or decision value MUST carry an explicit state. Unknown numeric values MUST be represented as `state: "UNKNOWN"` and `value: null`; zero is never an unknown-value substitute.
2. No engineering field has an implicit domain default. A structurally required property that is absent is a schema error. A present but unknown semantic value remains schema-valid and is evaluated as `UNKNOWN` or `BLOCKED` according to the rule.
3. The schema validator MUST validate structure and referential integrity but MUST NOT silently repair a hardware conflict. Domain conflicts are inputs to the rule engine.
4. A rule result MUST contain its rule identifier, rationale, inputs used, evidence used, limitations, and next action.
5. A candidate `PASS` MUST be checked by `RULE-EVIDENCE-001`. Missing or insufficient decisive evidence cannot become `PASS`.
6. Rule evaluation MUST be deterministic for the same canonical configuration, registry version, scenario definition, and evidence snapshot.
7. Runtime time stamps, absolute file paths, array input order where order is semantically irrelevant, and report formatting MUST NOT alter the technical result hash.
8. Research recommendations and GitHub examples MUST remain distinct from verified implementation results.
9. `ASSUMPTION`, `SECONDARY_SOURCE`, `HANDOFF_ONLY`, `UNKNOWN`, or `CONFLICT` alone MUST NOT support a hardware-validation claim.
10. D1 tests are specifications only and remain `NOT_RUN` until D2 executes them.

## 3. Configuration contract

The machine-readable contract is `WF01-D1-configuration-schema.json`, using JSON Schema Draft 2020-12. The schema intentionally separates structural validity from rule compliance so negative fixtures can be schema-valid and still produce rule-level `FAIL`.

### 3.1 Evidenced values

Customer and decision values use an `EvidencedValue<T>` object:

```json
{
  "state": "KNOWN",
  "value": 10,
  "unit": "channel",
  "evidence_refs": ["REQ-004-C"],
  "reason": null,
  "next_action": null
}
```

Allowed states are:

- `KNOWN`: `value` is type-correct and non-null; at least one resolvable evidence reference is required.
- `UNKNOWN`: `value` is null; `reason` and `next_action` are required. No numeric or Boolean value is inferred.
- `NOT_APPLICABLE`: `value` is null; the non-applicability decision and its evidence are recorded.
- `CONFLICT`: `value` is null; conflicting references and the resolution action are recorded.

### 3.2 Input areas

| Area | Required fields and types | Required? | Default policy | Structural and semantic validation |
|---|---|---:|---|---|
| Contract identity | `contract_version: string`, `configuration_id: string` | Yes | No default | Semantic version and stable ID syntax |
| MCU and module | `target.mcu_family`, `module_variant`, `silicon_revision`, `psram_present` as evidenced values; `module_profile_ref: string` | Yes | Unknown values are explicit | N8-specific rules are not assumed when the module is unknown |
| USB role | `usb.role`, `phy`, `simultaneous_usb_serial_jtag_required`, data-pin values, boot role, VBUS source/current-limit inputs | Yes | No role or current default | Enum validation; data-pin conflicts remain rule inputs |
| Debug and JTAG | transport list, `jtag_policy`, live-debug requirement, pad-JTAG GPIO list | Yes | `UNDECIDED` is explicit, never implicit | Unique transports and pins; policy consistency is rule-evaluated |
| eFuse strategy | `usb_phy_sel_strategy`, IO35-37 verification, irreversible-change approval reference | Yes | No burn/preserve default | `BURN_FOR_OTG` requires an approval reference; schema does not recommend burning |
| GPIO | capability profile, reservations, assignments, boot behavior, sharing identifier | Yes | Empty means explicitly no records, not unknown hardware | Unique IDs; pin range; duplicate/conflicting use checked by validator and rules |
| Serial topology | topology, requested channel/stream counts, native UART inventory, reserved controllers, alternative serializer resources | Yes | No channel or capacity default | Counts are non-negative integers; only validated known capacity is eligible for capacity rules |
| TFT | provisioning, interface, signal list, dedicated GPIO count, touch and PSRAM needs, Q-C11 decision reference | Yes | DNP does not create a pin-count default | Known count must match unique counted signals; unknown interface cannot yield rule `PASS` |
| Power input | domain, nominal/min/max voltage, suppression state, transient/jump-start/reverse-polarity references, functional policy | Yes | Only explicit values are allowed | If all voltages are known, `min <= nominal <= max`; no rail calculation is performed |
| Evidence catalog | evidence ID, source file, locator/claim IDs, exact evidence class, integrity state, optional SHA-256, upstream refs, supported inputs/rules | Yes | No inferred evidence | Unique IDs, exact class enum, resolvable references, valid SHA-256 syntax |
| Evaluation policy | `missing_value_policy`, determinism, pass scope, registry version, selected scenarios | Yes | Constants must be explicit | `DO_NOT_INFER`, `RULE_ONLY`, and deterministic operation are contract invariants |

### 3.3 Structural validation outcomes

- Invalid JSON or a missing required structural property: configuration validation `FAIL`; rule and scenario execution `NOT_RUN`.
- Valid structure with `state: UNKNOWN`: schema passes; the owning rule returns `UNKNOWN` or `BLOCKED` according to its dependency semantics.
- A duplicate identifier or unresolved evidence reference: validation `FAIL`; no affected rule may return `PASS`.
- A complete but conflicting hardware assignment: schema may pass; the applicable rule returns `FAIL`.

## 4. Rule registry contract

The machine-readable registry is `WF01-D1-rule-registry.json`. Every rule record contains the required fields:

- `rule_id`
- `description`
- `inputs`
- `condition`
- `expected_result`
- `evidence_refs`
- `exceptions`
- `test_ids`

The condition format is the bounded declarative profile `wf01-expression-v1`. It supports path lookup, equality, membership, Boolean composition, uniqueness, set exclusion, and numeric comparison/sum. D2 MUST reject an unknown operator rather than execute arbitrary code. Free-text descriptions explain intent but are not executable truth sources.

The initial registry contains exactly eight rules:

| Rule | Scope | Principal source links |
|---|---|---|
| `RULE-GPIO-001` | Reserve GPIO19/20 for internal USB D-/D+ | C03-004; C11-012; CLAUDE-11a GPIO19/20; IC-04 |
| `RULE-GPIO-002` | Prevent GPIO45 boot-high use on N8 | C11-005; CLAUDE-11a GPIO45 |
| `RULE-GPIO-003` | Prevent high-impedance GPIO3 at boot | C11-006; CLAUDE-11a GPIO3 |
| `RULE-USB-001` | Detect internal PHY Host/USB-Serial-JTAG sharing conflict | C03-003/005/010; C11-012/013/014 |
| `RULE-USB-002` | Preserve UART0 recovery on GPIO43/44 | C03-010; C11-014/015; REQ-003-I2; IC-06 |
| `RULE-SERIAL-001` | Compare independent TX demand with verified serializer capacity | C03-020/021; REQ-004-C; CF-09 |
| `RULE-TFT-001` | Prevent undefined TFT interface/pin demand from passing | PROP-02; IC-12; Q-C11 |
| `RULE-EVIDENCE-001` | Prevent missing or insufficient evidence from becoming PASS | CLAUDE-06 evidence model; WF01 traceability records |

These rules are architecture-contract checks, not definitive hardware verification.

## 5. Decision result contract

### 5.1 Status meanings

| Status | Meaning |
|---|---|
| `PASS` | The identified rule condition is satisfied for the recorded inputs and evidence. Scope is `RULE_ONLY`. |
| `FAIL` | Complete known inputs demonstrate an explicit rule violation. |
| `UNKNOWN` | Applicability or a value needed to determine the result is not known. |
| `BLOCKED` | The rule is applicable, but an external decision, conflict, approval, or decisive evidence prevents closure. |
| `NOT_RUN` | The evaluator was not invoked, the configuration was structurally invalid, the rule was not applicable, or a required test has not been executed. |

### 5.2 Required result fields

```json
{
  "result_contract_version": "1.0.0",
  "result_id": "RES-EXAMPLE",
  "configuration_id": "CFG-EXAMPLE",
  "configuration_sha256": "<canonical SHA-256>",
  "registry_version": "1.0.0-draft",
  "scenario_id": "SCN-USB-003",
  "rule_id": "RULE-USB-001",
  "applicability": "APPLICABLE",
  "status": "FAIL",
  "reason_code": "INTERNAL_PHY_CONCURRENT_OWNERS",
  "rationale": "Internal OTG Host and USB-Serial/JTAG are both requested concurrently.",
  "inputs_used": ["/usb/role", "/usb/phy", "/debug/programming_log_transports"],
  "evidence_used": ["CLAUDE-11#C11-012", "CLAUDE-11#C11-013"],
  "limitations": ["Architecture rule only; no hardware test was run."],
  "next_action": "Select a non-concurrent debug policy or provide a reviewed external-PHY architecture.",
  "pass_scope": "RULE_ONLY",
  "execution_status": "COMPLETED"
}
```

Required fields are `result_contract_version`, `result_id`, `configuration_id`, hashes/versions, `scenario_id`, `rule_id`, `applicability`, `status`, `reason_code`, `rationale`, `inputs_used`, `evidence_used`, `limitations`, `next_action`, `pass_scope`, and `execution_status`.

### 5.3 Scenario aggregation

Aggregation is deterministic and does not upgrade scope:

1. If the scenario was not invoked or configuration validation failed, the scenario is `NOT_RUN`.
2. If any applicable rule is `FAIL`, the scenario is `FAIL`.
3. Otherwise, if any applicable rule is `BLOCKED`, the scenario is `BLOCKED`.
4. Otherwise, if any applicable rule is `UNKNOWN`, the scenario is `UNKNOWN`.
5. Otherwise, if all applicable rules are `PASS`, the scenario is `PASS`.
6. If no rule is applicable, the scenario is `NOT_RUN`.

G1 and product approval are outside this aggregation.

## 6. Scenario contract

The inventory is `WF01-D1-scenario-matrix.csv`. Each record contains:

- `scenario_id`: stable unique identifier.
- `source_scenario_ids`: original report row or rows.
- `scenario_name`: short human-readable name.
- `input_conditions`: machine-oriented condition summary for fixture design.
- `decision_dependencies`: customer questions, decisions, gates, or external analyses.
- `evidence_refs`: local and external evidence identifiers.
- `evaluation_type`: controlled type of evaluation.
- `expected_result_policy`: status behavior, not an executed result.
- `release_target`: `MVP` or `BACKLOG`.
- `source_status`: source report's status wording.
- `normalization_note`: mapping or scope qualification.

The source report has 31 physical scenario rows. The two Q-C04 USB VBUS alternatives are branches of one decision and are represented by one `SCN-VBUS-001` contract with two `device_scope` branches. All source content is retained, yielding exactly 30 logical scenario contracts.

The MVP contains exactly four scenarios:

1. `SCN-USB-003`: internal USB PHY resource conflict.
2. `SCN-USB-002`: pad-JTAG reservation and GPIO-budget violation under the recorded 25-demand/24-supply assumption profile.
3. `SCN-SER-003`: ten independent TX streams versus verified serializer capacity.
4. `SCN-TFT-005`: undefined TFT interface and pin demand.

All remaining scenarios are backlog items. Backlog placement is not a rejection of the requirement or evidence.

## 7. Determinism and reproducibility

D2 SHOULD use RFC 8785-compatible JSON canonicalization or an equivalently specified canonical representation. The product MUST record:

- configuration SHA-256;
- rule-registry version and SHA-256;
- scenario-matrix version and SHA-256;
- evidence snapshot identifiers and integrity states;
- engine version;
- ordered results sorted by `scenario_id`, then `rule_id`.

Semantically unordered arrays, including evidence references and selected scenario IDs, are normalized by stable identifier before hashing. Source text, absolute paths, and timestamps are excluded from the technical result hash unless explicitly declared as decision inputs.

## 8. D1 acceptance boundary

D1 is complete when the eight requested rules, thirty logical scenarios, four MVP selections, result contract, module architecture, test specifications, REP-02 boundary, open decisions, and completion report are present and internally consistent. D1 completion does not authorize D2 automatically. D2 requires review and explicit approval of this contract set.
