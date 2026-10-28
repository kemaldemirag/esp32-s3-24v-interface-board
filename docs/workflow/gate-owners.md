# Gate owners and decision rights

Names are deliberately unassigned. The project owner must replace role placeholders with named people and confirm that the approver is authorized and independent where required. `TBD` is not an assignment.

| Gate | Scope / exit authority | Responsible owner (one person) | Approver | Required contributors | Current status |
|---|---|---|---|---|---|
| G0 | Repository/source intake and scope baseline | `TBD — Project Owner` | `TBD — Project Approver` | Evidence Owner, Configuration Manager | `UNKNOWN` |
| G1 | Requirements, source integrity, interfaces, blockers, evidence sufficiency; may authorize bounded G2 entry | `TBD — System Architect` | `TBD — Project Owner / Customer` | Customer, Hardware Lead, Firmware Lead, Evidence Owner, Quality | `BLOCKED` |
| G2 | Rail budget, exact-part decisions, timing and schematic design entry/readiness | `TBD — Hardware Lead` | `TBD — System Architect` | Power Engineer, Firmware Lead, Sourcing, Evidence Owner | `NOT_STARTED` |
| G3 | Pre-layout design review and PCB layout release | `TBD — PCB Lead` | `TBD — Hardware Lead` | Hardware, Firmware, Manufacturing, Quality | `NOT_STARTED` |
| G4 | DFM/manufacturing outputs and design verification | `TBD — Hardware Lead` | `TBD — Quality / Project Owner` | PCB Lead, Manufacturing, Test | `NOT_STARTED` |
| G5 | Bench/variant/release scope as applicable | `TBD — Test Lead` | `TBD — Quality / Project Owner` | Hardware, Firmware, Manufacturing | `UNKNOWN — scope decision required` |
| G6 | Reproducibility, provenance, license, final evidence package | `TBD — Evidence Owner` | `TBD — Project Owner / Quality` | All artifact owners | `NOT_STARTED` |

## Assignment and escalation rules

- Assign one accountable owner and one approver for every active gate; record names, organization/role, and acceptance authority in the approval record.
- A contributor may supply evidence but cannot silently approve their own unresolved high-risk decision. Record any approved exception.
- A gate owner may return a gate to `BLOCKED` when a prerequisite, evidence hash, or acceptance criterion changes.
- A status change requires a dated decision, evidence snapshot, rationale, and approver. Workflow tooling cannot grant approval.
- Until assignments are approved, no G1 `PASS` or G2 start is authorized.
