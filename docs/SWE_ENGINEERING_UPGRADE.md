# Intermediate SWE upgrade: design review

## Key refactors

1. **Pure Java protocol codec** (`BoardProtocol.java`): CRC framing and response decoding can be tested on a desktop JVM without Android runtime. `MainActivity` now deals with Bluetooth lifecycle and user interaction rather than reimplementing the wire format.
2. **Request correlation**: each outgoing request is tracked by sequence ID, `ITEM` streams don't finish a request, `ACK/ERR` finish it, and the socket closes after a five-second timeout. A single-threaded executor serializes outbound writes. Each socket owns its pending request, so cleanup from an old connection cannot clear a new connection.
3. **Validation**: commands and replies are length-bounded; firmware rejects malformed fields, surplus arguments, non-ASCII bytes and numeric overflow.
4. **EEPROM endurance**: temporary notices no longer trigger EEPROM writes, and an unchanged permanent snapshot is not rewritten. `EEPROM.update()` is used to avoid changing matching cells.
5. **Nonblocking firmware loop**: bounded serial receive work, cooperative periodic scheduling and scrolling via unsigned `millis()` subtraction; no `delay()`.
6. **Bounded memory**: five fixed-size notice slots and a bounded receive frame; no heap allocation in firmware's hot path.

## Real limitations (disclose in interview)

- HC-05 Classic SPP requires Android Classic Bluetooth; it will not work with iOS or BLE-only devices.
- Existing protocol responses have no CRC and accept unauthenticated paired clients. Pairing is NOT a replacement for application authorization.
- A timeout is not a failed transaction: the board may have applied the command and the ACK may have been lost. Do **not** retry non-idempotent operations automatically without deduplication.
- Android cycles through sequence IDs 1–255. A very late matching ACK could be mistaken for a newer request after wraparound. Production needs session IDs and deduplication.
- EEPROM commits alternate between two banks and preserve the prior valid snapshot during an interrupted write. EEPROM endurance remains finite.
- High-priority notices can starve low-priority notices. This is a deliberate strict-priority policy, not a fairness guarantee.
- `millis()`-based temporary TTLs reset on power loss and temporary notices are RAM-only. For wall-clock expiry, add RTC/time sync.
- LCD I2C writes and SoftwareSerial writes are synchronous, so the scheduler is cooperative, not real-time deterministic.

## Interview explanation

“Android uses a dedicated I/O executor for RFCOMM writes and a receiver thread for asynchronous replies. I separated the wire protocol into a pure Java codec, with CRC-8 and sequence-based correlation. On Arduino, a bounded receiver parses newline-delimited frames, validates all fields, dispatches CRUD operations, and schedules notices cooperatively. EEPROM stores permanent entries only, using change detection and per-byte updates to reduce wear. I documented reliability limitations such as at-most-one outstanding sequence, lost acknowledgements and finite EEPROM endurance.”

## Tests

Run `bash tests/run_tests.sh` with Python 3, JDK 17, and a C++11 compiler installed. The suite validates the reference encoder, Java codec, and firmware running against simulated hardware. Hardware, Android lifecycle and MCU builds still need their own toolchains and physical integration tests.
