# WF01-D1 Digital Product Architecture

**Product:** ESP32-S3 Interface Architecture Decision Workbench  
**Architecture version:** 1.0.0-draft  
**Stage:** WF01-D1  
**Status:** Specification only; no product code or live REP-02 integration exists.

## 1. Architecture goals

The workbench converts evidence-linked interface requirements into deterministic, reviewable architecture decisions. It must preserve unknowns, separate structural validation from engineering evaluation, trace every result to inputs and sources, and never elevate a rule result into hardware or project-gate approval.

The architecture is deliberately offline and file-oriented for the MVP. Network research, source acquisition, PDF/Word parsing, PCB design, simulation, and bench instrumentation are outside the runtime.

## 2. Logical modules and responsibilities

### 2.1 Configuration Loader

**Owns**

- Reading one local UTF-8 JSON configuration.
- Recording the original file SHA-256 and loader version.
- Rejecting invalid encoding, duplicate JSON keys, non-finite numbers, or malformed JSON.
- Passing explicit `UNKNOWN`, `NOT_APPLICABLE`, and `CONFLICT` states without alteration.

**Does not own**

- Supplying customer values.
- Fetching documents or resolving internet links.
- Choosing a module, pin, threshold, current, part, eFuse, or architecture.
- Repairing invalid JSON or applying domain defaults.

**Output:** `RawConfigurationEnvelope { source_sha256, parser_version, raw_object }` or a loader error. A loader error causes configuration `FAIL` and all rule/scenario evaluations `NOT_RUN`.

### 2.2 Schema Validator

**Owns**

- JSON Schema Draft 2020-12 validation using `WF01-D1-configuration-schema.json`.
- Required fields, types, controlled values, identifier syntax, and `additionalProperties: false`.
- Referential integrity: evidence IDs, scenario IDs, assignment/reservation IDs, and rule references.
- Cross-record invariants declared by the schema, including uniqueness and known voltage ordering.
- Producing a normalized configuration whose semantically unordered arrays are stably sorted.

**Does not own**

- Declaring a hardware conflict safe.
- Converting an `UNKNOWN` value into a schema failure when its evidenced-value object is complete.
- Evaluating the eight engineering rules.

**Output:** `ValidatedConfiguration` and `ValidationFinding[]`. Structural failure stops rule execution. Semantic unknowns proceed to the engine.

### 2.3 Rule Registry

**Owns**

- Loading exactly versioned rule definitions from `WF01-D1-rule-registry.json`.
- Rejecting duplicate rule IDs, unresolved test IDs, unresolved evidence IDs, or unknown DSL operators.
- Exposing descriptions, inputs, conditions, expected results, evidence references, exceptions, and test bindings.
- Pinning the registry SHA-256 in every run.

**Does not own**

- Running arbitrary script embedded in a rule.
- Network or file-system operations from condition expressions.
- Rail-budget, regulator-derating, stability, power-sequencing, thermal, or load-step rules.

The `wf01-expression-v1` language is a closed declarative operator set. D2 must fail registry loading on an unknown operator instead of interpreting free text or executing code.

### 2.4 Constraint Evaluation Engine

**Owns**

- Evaluating applicable registered rules as pure functions of the validated configuration, registry, and evidence snapshot.
- Producing one rule-scoped result with a status, reason, decisive inputs, evidence, limitations, and next action.
- Applying `RULE-EVIDENCE-001` to every candidate PASS.
- Using stable reason codes and stable result ordering.

**Does not own**

- Selecting architecture options on the user's behalf.
- Treating examples or research proposals as implemented hardware.
- Inferring missing values or counting research-only serializer capacity as validated.
- Producing G1, compliance, PCB, or product approval.

**Output:** `RuleResult[]` conforming to the Decision Result Contract.

### 2.5 Scenario Runner

**Owns**

- Loading the 30 logical records in `WF01-D1-scenario-matrix.csv`.
- Selecting the four MVP scenarios or an explicitly supplied scenario set.
- Resolving decision dependencies and invoking the applicable registered rules.
- Evaluating typed scenario assertions that are explicitly part of the scenario contract.
- Aggregating results with `FAIL > BLOCKED > UNKNOWN > PASS > NOT_RUN` and recording all component results.

**Does not own**

- Hiding new engineering rules in presentation code.
- Converting backlog items into requirements.
- Generalizing the source-specific 25-demand/24-supply GPIO profile.

`SCN-USB-002` exposes a D1 boundary decision: its aggregate GPIO comparison must be either a declared typed scenario assertion or a future reviewed rule. The D2 implementation choice is blocked by `OD-D1-008`.

### 2.6 Evidence & Report Generator

**Owns**

- Resolving evidence IDs to source paths, claim locators, classes, and integrity states.
- Producing human- and machine-readable reports from immutable evaluation results.
- Recording configuration, registry, scenario-matrix, and evidence-snapshot hashes.
- Separating source facts, derived claims, engineering rules, assumptions, unknowns, and conflicts.
- Adding claim limits and next actions without changing the technical status.

**Does not own**

- Editing source evidence.
- Treating report prose as an executable rule.
- Upgrading a REP-02 check, rule PASS, or scenario PASS into G1.

## 3. Data flow

```text
configuration.json + local evidence catalog
                    |
                    v
          Configuration Loader
                    |
          raw object + input hash
                    |
                    v
             Schema Validator  -----> validation findings
                    |
     canonical validated configuration
                    |
                    +-------------------------+
                    |                         |
                    v                         v
       Constraint Evaluation Engine <---- versioned Rule Registry
                    |
              rule results
                    |
                    v
              Scenario Runner <--------- versioned scenario matrix
                    |
        scenario results + dependencies
                    |
                    v
        Evidence & Report Generator
                    |
       +------------+----------------+
       |                             |
       v                             v
REP-01 decision report       optional REP-02 request file
                                          |
                                          v
                                REP-02 external assessment
                                          |
                                          v
                              attached response evidence only
```

The technical result hash is calculated from canonical input, registry, selected scenario contracts, evidence snapshot, and engine version. Report timestamps and absolute paths are excluded.

## 4. Principal internal data contracts

| Contract | Producer | Consumer | Required controls |
|---|---|---|---|
| `RawConfigurationEnvelope` | Configuration Loader | Schema Validator | Original SHA-256; duplicate-key rejection; no mutation |
| `ValidatedConfiguration` | Schema Validator | Engine and Scenario Runner | Schema version; canonical ordering; resolved references |
| `RuleDefinition` | Rule Registry | Engine | Stable `rule_id`; closed DSL; registry hash; test bindings |
| `RuleResult` | Engine | Scenario Runner and Reporter | Rule-scoped status; reason code; decisive inputs/evidence; next action |
| `ScenarioDefinition` | Scenario matrix | Scenario Runner | Stable ID; dependencies; evaluation type; expected status policy |
| `ScenarioResult` | Scenario Runner | Reporter | Component results; aggregation trace; no hidden status upgrade |
| `EvidenceSnapshot` | Validator/Reporter | Engine and Reporter | Exact class; integrity state; source locator; local hash where available |
| `Rep02AssessmentRequest` | REP-01 adapter | REP-02 | Contract version; configuration hash; unknown-preserving power data |
| `Rep02AssessmentResponse` | REP-02 | REP-01 adapter | Echoed input hash; tool/model provenance; check-scoped results |

## 5. Status propagation

### 5.1 Configuration phase

- Malformed JSON or schema violation: configuration `FAIL`; rules and scenarios `NOT_RUN`.
- Structurally valid `UNKNOWN`: configuration validation succeeds; the owning rule returns `UNKNOWN` or `BLOCKED`.
- Unresolved cross-reference: validation `FAIL`, unless the reference belongs to an optional external assessment not selected for the run.

### 5.2 Rule phase

- Complete, known violation: `FAIL`.
- Known applicability with an external decision, decisive evidence, or required value absent: `BLOCKED`.
- Applicability itself unknown: `UNKNOWN`.
- Condition satisfied and evidence gate satisfied: `PASS` with `pass_scope=RULE_ONLY`.
- Not invoked or not applicable: `NOT_RUN`.

### 5.3 Scenario phase

The scenario record retains every component result. Aggregation uses the declared precedence and does not discard lower-severity evidence. A scenario with no applicable rule is `NOT_RUN`, not PASS.

## 6. REP-01 and REP-02 responsibility boundary

REP-02 is currently a scaffold-only repository. No REP-02 models, code, customer schematic, or executed tests are available, and the referenced PKG-A CSV inputs are not locally accessible. D1 therefore defines a future file contract only.

| Concern | REP-01 workbench | REP-02 power-rail-validation-and-review |
|---|---|---|
| MCU/module selection state | Records and evaluates interface consequences | Consumes only when relevant to load/thermal inputs |
| GPIO, boot straps, USB PHY, JTAG, eFuse | Owns architecture constraints | Out of scope |
| TTL/RS-232 topology and UART/serializer capacity | Owns | May consume resulting rail loads |
| TFT interface and pin demand | Owns | May consume a defined display load |
| Customer power-input profile | Records without inference | Consumes and checks for its analyses |
| Rail demand and 24 V → 5 V → 3.3 V topology | Records consumers and interface contract | Owns budgets, margins, and derating |
| EN/PGOOD/RESET intent | Records signal ownership and logical contract | Owns timing/sequencing assessment when inputs exist |
| Regulator stability and compensation | Must not implement | Owns |
| Thermal, load-step, input-sag, backfeed analysis | Must not implement | Owns |
| Model source, license, hash, and validity bounds | Validates received metadata | Produces and owns |
| Hardware-validation claim | Does not produce | Does not produce from simulation alone |
| G1/G2/G3/G5 decision | Project gate owner only | Supporting evidence only |

The 5 V to 3.3 V ordering observed in research is a system-topology derivation, not an ESP32-S3 data-sheet requirement. Both products must preserve that classification.

## 7. Proposed REP-01 → REP-02 request contract

The request is a versioned JSON document. Fields that are not known use the same evidenced-value model as the REP-01 configuration.

```json
{
  "contract_version": "1.0.0-draft",
  "handoff_id": "REP01-REP02-REQUEST-ID",
  "producer": {
    "repo": "esp32-s3-24v-interface-board",
    "product_contract_version": "1.0.0-draft",
    "rule_registry_version": "1.0.0-draft"
  },
  "configuration_id": "CONFIGURATION-ID",
  "configuration_sha256": "64-hex-sha256",
  "power_input_profile": {},
  "rail_demands": [],
  "power_control_signals": [],
  "temporal_constraints": [],
  "interface_power_behaviors": [],
  "requested_analyses": [],
  "open_questions": [],
  "blockers": [],
  "evidence_snapshot": [],
  "claim_limits": [
    "No hardware-validation claim",
    "Unknown values have no numeric default"
  ]
}
```

### 7.1 Request collection fields

| Collection | Required item fields | Meaning |
|---|---|---|
| `rail_demands` | `rail_id`, `consumer_id`, `operating_mode`, `voltage_requirement`, `normal_current`, `peak_current`, `peak_duration`, `state`, `evidence_refs` | Consumer demand without REP-01 calculating margin |
| `power_control_signals` | `signal_id`, `kind`, `source_domain`, `destination_domain`, `active_level`, `off_state`, `state`, `evidence_refs` | EN/PGOOD/RESET ownership and intent |
| `temporal_constraints` | `constraint_id`, `from_event`, `to_event`, `min_delay`, `max_delay`, `classification`, `evidence_refs` | Customer or derived timing constraints with explicit classification |
| `interface_power_behaviors` | `interface_id`, `sources_power`, `backfeed_policy`, `off_state`, `state`, `evidence_refs` | USB VBUS, display, and external-interface power behavior |
| `requested_analyses` | controlled values such as `RAIL_BUDGET`, `DERATING`, `SEQUENCING`, `LOAD_STEP`, `INPUT_SAG`, `BACKFEED_RISK` | A request, not proof that an analysis ran |

## 8. Proposed REP-02 → REP-01 response contract

```json
{
  "contract_version": "1.0.0-draft",
  "assessment_id": "REP02-ASSESSMENT-ID",
  "input_configuration_sha256": "64-hex-sha256",
  "tool_versions": [],
  "model_records": [],
  "checks": [],
  "scenario_results": [],
  "findings": [],
  "summary_status": "PASS|FAIL|UNKNOWN|BLOCKED|NOT_RUN",
  "claim_limits": [],
  "generated_at": "RFC-3339-date-time"
}
```

Every `check` and `scenario_result` must contain `id`, `status`, `reason`, `inputs_used`, `metrics`, `evidence_refs`, `model_refs`, `limitations`, and `next_action`. Every `model_record` must contain `model_id`, `source`, `sha256`, `license_status`, `model_type`, `validity_bounds`, and `access_status`.

REP-01 must reject a response whose `input_configuration_sha256` differs from the request. Missing model provenance or license state blocks physical claims. REP-02 PASS remains check-scoped and never changes G1. `generated_at` is report metadata and is excluded from the deterministic technical hash.

## 9. Versioning and compatibility

- Contract files use semantic versions with a `-draft` suffix during D1 review.
- A field removal, status semantic change, rule-condition change, or DSL operator change requires a major version.
- Additive optional metadata requires a minor version.
- Text correction with no machine-semantic effect requires a patch version.
- A run pins the exact schema, registry, scenario matrix, and evidence snapshot hashes.
- Migration must be explicit and produce a before/after audit record; the loader does not silently upgrade input.

## 10. Security and safety controls

- No rule expression may execute shell, network, file-system, or dynamic-language code.
- Input size, nesting depth, array length, and evaluation-step limits must be bounded in D2.
- Reports must escape untrusted source text and must not treat document content as instructions.
- eFuse fields describe decisions only; the workbench has no programming interface.
- The runtime is read-only with respect to evidence and design repositories.

## 11. D2 transition boundary

After human review of the D1 contract set, D2 may implement the loader, schema validator, closed registry interpreter, pure evaluation engine, four MVP scenarios, synthetic fixtures, and deterministic report generation.

D2 remains **NO-GO** for live REP-02 execution, real rail or thermal claims, eFuse provisioning, KiCad generation, compliance conclusions, and hardware-validation claims until separate inputs, ownership, and tests are approved.
