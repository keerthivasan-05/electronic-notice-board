# Verification and demonstration test plan

## Automated tests

Run `bash tests/run_tests.sh` with Python 3, JDK 17, and a C++11 compiler. The suite checks CRC and reply parsing, runs the firmware against simulated serial input and EEPROM, and interrupts each changed-byte write during legacy migration and a subsequent commit. It also covers expiry and generation rollover. Use the acceptance tests below to verify the physical board and Android app.

## Hardware acceptance tests (manually execute and record evidence)

| ID | Steps | Expected result |
|---|---|---|
| T01 | Pair HC-05; connect in app | Bluetooth connected status |
| T02 | Send PING | `ACK|seq|PONG` |
| T03 | ADD ID 1 / P2 / TTL 0 / `Welcome` | `SAVED`, LCD shows notice |
| T04 | ADD ID 1 with updated text | Original ID overwritten; count remains 1 |
| T05 | ADD ID 2 / P3 / TTL 1 | ID 2 displayed ahead of ID 1 |
| T06 | Add ID 3 / P3 / TTL 0 | ID 2 and ID 3 alternate approximately every 4 s |
| T07 | Wait ~60s after T05 | ID 2 removed from active state |
| T08 | STATUS | COUNT matches active store |
| T09 | LIST | ITEM lines for each active ID, final `ACK|seq|END` |
| T10 | DELETE unknown ID | `ERR|seq|NOT_FOUND` |
| T11 | Fill 5 slots then add sixth ID | `ERR|seq|FULL` |
| T12 | Reboot after saving permanent + temporary items | Only permanent items restored |
| T13 | Send frame with corrupted CRC via test transmitter | `ERR|0|CRC` |
| T14 | Send >107 bytes without newline, then newline | `ERR|0|OVERSIZE`; next frame accepted |
| T15 | Send non-ASCII or reserved delimiters via Android | App rejects before transmit |
| T16 | Turn off Bluetooth during scrolling | Board continues displaying without app |
| T17 | Clear all notices | Blank/no active notice display; count 0 |
| T18 | Power-cycle during EEPROM write (controlled lab only) | The previous valid snapshot survives interruption before the new commit marker |

## Evidence to collect

Photo of wiring; short video showing priority preemption, scroll, timeout and reboot; screenshot of Android ACK logs; source commit hash; build output from Android Studio and Arduino IDE; actual test matrix with PASS/FAIL and hardware details. **Do not label tests passed before they run on your setup.**

## Boundary / failure analysis

- ASCII-only text; not suitable for Tamil/Unicode without a different character display and encoding strategy.
- `millis()` is uptime-based and absent during power loss.
- A lower priority message may starve; this is deliberate.
- Android disconnects on request timeout and reports an unknown outcome. The app does not automatically retry.
- A stolen paired device or weak PIN undermines access control.
- EEPROM cells have finite write endurance. Two banks protect against interruption of one commit.
- Serial receive buffering can overflow during burst traffic; run sustained-load tests if needed.
