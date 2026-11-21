# Technical walkthrough: from UI event to LCD pixels

## 1. Problem statement and objectives

Traditional notice boards need a person physically present to swap papers. This system lets an operator update notices within HC-05 Bluetooth range, without a network subscription or Wi-Fi. Functional requirements: create, replace, delete, list, display, prioritize and expire notices. Nonfunctional requirements: bounded MCU RAM usage, no blocking delays in the display loop, command integrity checks and reboot persistence for permanent data.

## 2. Software architecture

**Presentation layer:** Android `Activity` creates UI controls; `EditText` collects notice IDs, TTL and message; `Spinner` controls priority. App validates bounds and ASCII before transmission.

**Transport layer:** Android `BluetoothSocket` uses the standard SPP service UUID. Android talks RFCOMM, an ordered *stream* of bytes; HC-05 converts it to UART. Stream data may arrive in arbitrary chunks. On AVR, `SoftwareSerial` reconstructs incoming characters, and a newline-terminated parser assembles them into frames. Never equate a single read with a complete command.

**Protocol layer:** requests include `@`, sequence number, pipe-separated command and arguments, `*` and CRC-8 hex, then newline. The receiving parser enforces maximum length and CRC, then dispatches by command string. CRC is computed over bytes between `@` and `*`. The Arduino sends `ACK`, `ERR` and `ITEM` lines. Example `@0|PING*B2\n` -> `ACK|0|PONG\n`.

**State layer:** `NoticeStore` manages five fixed slots. Each record holds ID (uint8), priority (uint8), NUL-terminated text (65 bytes), expiration time (uint32) and used flag. An add with an existing ID replaces it. TTL = 0 stores a permanent entry; temporary entries have deadlines based on `millis()`.

**Display layer:** main `loop()` calls `pollBluetooth()`, expires stale notices, selects a message every 4 s, and advances scrolling every 300 ms. It checks elapsed time rather than calling `delay()`; this allows commands to be handled while the LCD scrolls. First line has ID/priority; second scrolls the text with blank separators.

## 3. Scheduler details

When a display slot changes, `nextIndex(previous)` scans the store to find the maximum priority of active records. It then scans cyclically for a record with that priority. This gives round-robin fairness **within** the maximum priority class; lower priority notices are not selected while any higher priority exists. Complexity O(N) per switch with N=5. This is acceptable for a tiny embedded system and clearer than a heap or dynamic container. If lower classes must receive guaranteed airtime, use weighted round robin or time quotas.

## 4. State persistence and data integrity

`NoticeStore::persist()` stores permanent records in two 340-byte EEPROM banks. Each snapshot contains a commit marker, format version, record count, generation, fixed-size records, and a CRC-8. It writes to the inactive bank, then restores that bank's marker as the final commit step. On boot, firmware validates both banks and loads the newest valid generation, including across generation rollover.

Firmware can also read the original version-1 snapshot at address zero. Its first version-2 write targets the second bank, which preserves the legacy snapshot during migration. Temporary-only changes and identical permanent snapshots skip EEPROM writes. `EEPROM.update()` writes only changed bytes during a commit.

## 5. Why each technology was chosen

| Choice | Why | Alternative and trade-off |
|---|---|---|
| Arduino Uno | simple AVR platform with hardware I2C and internal EEPROM | ESP32 offers Wi-Fi/BLE and more RAM, but changes architecture |
| HC-05 SPP | straightforward serial link for Android Classic Bluetooth | BLE GATT requires a different radio and app protocol |
| Android Java | native access to Bluetooth permission and RFCOMM API | Kotlin is modern but outside original project's Java requirement |
| 16×2 LCD | inexpensive, deterministic demo hardware | LED matrix is more visible but needs scan/driver handling |
| Static arrays | predictable bounded memory, no heap fragmentation | dynamic lists are flexible but expensive on AVR |
| CRC-8 | low-cost accidental corruption detection | CRC-16 stronger, MAC/signature needed for authenticity |
| EEPROM | board survives power cycles without SD card | external FRAM better endurance, SPI flash scales capacity |
| `millis()` scheduler | responsive event loop without blocking | RTOS tasks unnecessary at this small scale |

## 6. Capacity and rough performance

- Store slots: 5 active notices.
- Message length: 64 printable ASCII characters per notice, not arbitrary Unicode.
- Frame size: up to 107 visible bytes plus NUL. The longest ADD frame is within this bound (up to ~91 bytes depending on parameter lengths).
- UART rate: commonly 9600 bits/s in HC-05 data mode; with 8N1 serial, nominally at most 960 bytes/s before transport overhead. Actual latency depends on buffering, interference and LCD I/O.
- Display rotation: every 4000 ms; scroll advancement: every 300 ms.
- EEPROM upper bound: 3 header bytes + 5×(3 metadata + 64 text) + CRC = 339 bytes.
- CPU: small O(N) scans with N=5; no dynamic allocation in firmware application logic.

## 7. Known technical caveats

1. The Arduino parser reads only 24 bytes per `loop()` call to avoid starvation; input buffering may still overflow if bursts outrun `SoftwareSerial`/display work. Avoid overly frequent commands; production could use hardware UART and a ring buffer.
2. The LCD library and EEPROM writes may block for short periods; the firmware is **cooperative**, not hard real-time.
3. Snapshot persistence invalidates the header before writing each time, even for a temporary-only change. This is a simplification; optimize in production.
4. `millis()` is 32-bit and wraps around about 49.7 days. Signed-difference expiry comparisons work for deadlines within 2^31 ms; TTL max 1440 minutes is safely under this limit.
5. Replies are plaintext ASCII without CRC; corrupted replies may be misread. Production should frame both directions.
6. Commands can be accepted from any paired Bluetooth client with access to the HC-05. For real deployments use an app-level authenticated protocol and suitable hardware.
7. Because an update acts immediately, repeated `ADD` updates ID state and restarts TTL: retransmitting after an ACK timeout would not be exactly-once.
8. `String.format` is used on Android, not on AVR; this is fine on the phone but would be wasteful on the MCU.

## 8. Source code walkthrough by method

- **`MainActivity.refresh()`**: checks Android Bluetooth permissions, requests enabling, displays bonded devices.
- **`connect()`**: makes RFCOMM socket connection on a background executor; never performs blocking connect on UI thread.
- **`readReplies()`**: reads newline-delimited ACK/ERR/ITEM responses on a dedicated thread; updates UI via main handler.
- **`send()`**: serializes outbound operations in one executor, assigns sequence ID, calculates CRC, writes ASCII bytes.
- **`crc8()` (Java/Arduino/Python)**: identical CRC-8/ATM bitwise algorithm; `123456789` check value is `F4`.
- **`pollBluetooth()`**: accumulates stream bytes into a fixed array, detects newline and buffer oversize.
- **`processFrame()`**: finds CRC suffix, validates bytes, tokenizes command fields and dispatches operations.
- **`NoticeStore::add()`**: replaces existing ID or locates free slot, updates expiry and triggers serialization.
- **`NoticeStore::expire()`**: drops temporary notices with elapsed deadlines.
- **`NoticeStore::nextIndex()`**: highest-priority selection and circular fairness.
- **`selectNext()` / `draw()`**: update display selection, reset animation state, render 16-character rows.
