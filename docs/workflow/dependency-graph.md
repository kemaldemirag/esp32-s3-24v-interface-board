# Workflow dependency graph

The graph expresses prerequisites and handoffs, not completion. A blocked node prevents dependent activities from being treated as approved.

```mermaid
flowchart TD
  G0["G0: scope, source intake, repository baseline"] --> EVID["Evidence catalog: provenance, integrity, licenses"]
  EVID --> Q["Q-C01–Q-C11: evidence-backed customer inputs"]
  EVID --> CF["CF-08 / CF-09 and open interface decisions"]
  Q --> CONTRACT["Freeze requirements, interface contract, GPIO/topology"]
  CF --> CONTRACT
  OWN["Assign gate owner, approver, acceptance criteria"] --> REVIEW["G1 closure review"]
  CONTRACT --> REVIEW
  EVID --> REVIEW
  RISKS["Risk review and approved deferrals"] --> REVIEW
  REVIEW -->|Explicit approved PASS| G2["G2: rail budget, exact-part decisions, schematic planning"]
  REVIEW -->|BLOCKED / FAIL| ACTIONS["Owners resolve evidence, input, or conflict"]
  ACTIONS --> Q
  ACTIONS --> EVID
  G2 --> PARTS["Controlled part selection + traceable datasheets"]
  G2 --> RAIL["Reviewed rail budget and thermal analysis"]
  G2 --> SCHEM["Schematic entry checklist and authorization"]
  PARTS --> SCHEM
  RAIL --> SCHEM
  SCHEM --> G3["G3: pre-layout review"]
  G3 --> G4["G4: DFM, outputs, and verification"]
  G4 --> G5["G5: bench/release scope (if applicable)"]
  G5 --> G6["G6: reproducibility and evidence package"]
  WF01["WF01-D1 rules/scenarios: rule-scoped evidence"] -. "input only; never gate approval" .-> REVIEW
```

## Change-impact rules

- Changes to input/transient profiles reopen the protection decision and dependent rail/thermal calculations.
- Changes to Q-C07, GPIO assignments, module variant, USB/debug policy, or Q-C11 reopen the relevant GPIO/interface decisions.
- Changes to load/current, temperature, or exact parts invalidate the dependent rail/thermal calculations and reviews.
- Changes to manufacturing/profile assumptions reopen affected DFM, protection, and test criteria.
- Every reopened item retains the old evidence snapshot for audit and receives a new revision; stale approvals are not silently reused.
