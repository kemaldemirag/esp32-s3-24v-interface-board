# Decision log and evidence tracking

This index is append-oriented. The detailed rationale and options live in one `design-decision-template.md` copy per decision. Never edit an approved decision in place; supersede it and retain the previous record.

| Decision ID | Date | Gate / requirement / Q-C | Question / decision | Status | Evidence IDs and revision | Owner | Approver | Review trigger / next action |
|---|---|---|---|---|---|---|---|---|
| CF-09 | 2026-10-16 | G1 / REQ-004-C / Q-C07 | Are the five TTL and five RS-232 TX outputs ten independent streams or five mirrored streams? | `BLOCKED` | `CLAUDE-03 C03-020`; `CLAUDE-06 REQ-004-C`; `docs/open-decisions.md` | `TBD` | `TBD` | Obtain approved topology and frozen GPIO record |
| D-JTAG | 2026-10-16 | G1 / GPIO budget | Reserve GPIO39–42 for pad JTAG? | `UNKNOWN` | `CLAUDE-11 C11-014`; `docs/open-decisions.md` | `TBD` | `TBD` | Decide debug/bring-up and production-access policy |
| D-USB-EFUSE | 2026-10-16 | G1 / USB recovery | What is the USB_PHY_SEL eFuse strategy? | `UNKNOWN` | `CLAUDE-11 C11-012/C11-015`; `docs/open-decisions.md` | `TBD` | `TBD` | Review recovery consequences; separate approval for any irreversible action |
| D-EFUSE-IO35-37 | 2026-10-16 | G1 / GPIO map | Has the N8 default eFuse function for GPIO35–37 been verified? | `UNKNOWN` | `CLAUDE-11 C11-003/C11-004` | `TBD` | `TBD` | Attach authoritative module/silicon evidence before assignment |
| D-ANTENNA | 2026-10-16 | G1 / module layout | What WROOM-1 antenna keepout applies? | `UNKNOWN` | `CLAUDE-11 C11-011` | `TBD` | `TBD` | Pin a primary design-guide source and locator |
| Q-C11 | 2026-10-16 | G1 / PROP-02 / IC-12 | Is the TFT connector needed, and what interface/signal set is required? | `UNKNOWN` | `docs/open-decisions.md`; `docs/interface-contract.md` | `TBD` | `TBD` | Obtain customer answer and approved signal list |

| New decision | Copy `design-decision-template.md`; link the copy here | | | | | | | |

Decision status vocabulary: `PROPOSED`, `APPROVED`, `REJECTED`, `SUPERSEDED`, `BLOCKED`, `UNKNOWN`. For technical rule/scenario results use the WF01-D1 contract's status vocabulary; do not conflate it with approval state.
