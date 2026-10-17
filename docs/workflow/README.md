# G1/G2 engineering workflow

**Status:** Working templates; G1 remains `BLOCKED`. These documents do not approve requirements, select parts, or authorize schematic entry by themselves.

Start here:

1. Assign a named owner and approver in [gate-owners.md](gate-owners.md).
2. Answer the canonical Q-C questions in [Q-C-response-template.csv](Q-C-response-template.csv), linking each answer to evidence. Preserve unknowns; do not infer missing values.
3. Work through [checklist-G1-closure.md](checklist-G1-closure.md) and record decisions with [design-decision-template.md](design-decision-template.md).
4. Use [gates-G1-G2-workflow.md](gates-G1-G2-workflow.md) for closure, handoff, and schematic-entry controls.
5. Continue evidence and approval tracking in [evidence-traceability.csv](evidence-traceability.csv) and the [approval record](approval-record-template.csv).

The Q-C numbering in this workflow follows `docs/open-decisions.md`, `docs/interface-contract.md`, and `requirements/requirements-traceability.csv`. In particular, Q-C06 is the thermal environment, Q-C07 is serial topology, and Q-C11 is the TFT interface. Where older conversation summaries disagree, the checked-in records are authoritative.

## Review loop

- [Weekly review template](weekly-review-template.md)
- [Decision log](decision-log.md)
- [Risk register](risk-register.csv)
- [Dependency graph](dependency-graph.md)
- [GitHub Actions preparation placeholder](github-actions-placeholder.yml)
- [Workflow examples and worksheet](workflow-examples/)

No real owner, customer response, approval, measured value, selected part, or verification result is implied by a blank template or illustrative example.
