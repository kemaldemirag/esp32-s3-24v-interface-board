# Worked reference example: RejsaCAN

**Type:** Reconstructed public-information walkthrough, not the project's internal decision history and not an endorsement. No RejsaCAN part or circuit is selected for this board.

**Source reviewed:** [MagnusThome/RejsaCAN-ESP32 README](https://github.com/MagnusThome/RejsaCAN-ESP32/blob/main/README.md), inspected 2026-10-17.

## Publicly documented facts

The project README describes an ESP32/ESP32-S3 board for vehicle use, an onboard CAN interface, vehicle input described as 5–24 V, optional automatic shutdown, and downloadable schematics, Gerbers, and BOM. It also says that the appropriate adjustment for a 24 V vehicle is not known in that project. These are statements from the README, not independently verified measurements or a JOB-01 requirement.

## Reconstructed decision path

| Step | Example decision artifact | What this project's workflow should retain |
|---|---|---|
| Define use case | Vehicle/OBD2 use and battery-drain concern | Product domain and powered/parked behaviors; this is not a substitute for CF-08/Q-C01–Q-C03 |
| Bound the input | README states 5–24 V and exposes a threshold-adjustment concern | Separate nominal range from transient, reverse-polarity, jump-start, and suppression evidence; explicitly keep unknowns open |
| Define power behavior | Optional hardware shutdown plus software-controlled alternatives are described | Record functional policy, startup/shutdown behavior, and load profiles before selecting architecture |
| Freeze interfaces | CAN, UART/debug, USB and GPIO usage are documented in project materials | Map every interface, pin reservation, and conflict; compare to this board only after Q-C07/GPIO closure |
| Package design artifacts | Schematics, Gerbers, and BOM are linked | Capture revision, license/provenance, exact design variant, hashes, and evidence class before reuse |

## Transferable lesson and limits

Useful workflow pattern: translate use-case behavior into an explicit input/power/interface contract, carry unresolved 24 V conditions into a decision gate, and keep design artifacts tied to a revision. The project's stated 24 V capability does not prove JOB-01's transient, automotive, protection, or thermal requirements. Its circuit, thresholds, BOM, and pinout must not be copied as a validated solution.

**Evidence status:** `SECONDARY_SOURCE` for workflow inspiration; no claim is elevated to `SOURCE_EXPLICIT` for this project's requirements.
