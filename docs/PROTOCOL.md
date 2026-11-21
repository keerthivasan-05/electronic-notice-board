# Protocol specification (v2)

Bluetooth Classic SPP runs as an RFCOMM byte stream, **not** a packet transport; UART at 9600 baud connects HC-05 to UNO SoftwareSerial.

## Requests

`@<seq>|<command>[|arguments]*<crc>\n`

- `seq` is decimal 0–255. Android cycles through 1–255 so it can ignore uncorrelated errors, which use sequence 0.
- `crc` two uppercase hex digits, CRC-8/ATM polynomial 0x07, initial 0x00, calculated over the exact bytes between `@` and `*` (exclusive).
- Printable ASCII only. `|`, `*`, CR, LF are forbidden inside notice text.
- `ADD|id|priority|ttl_minutes|text`: id 1–255; priority 1..3; TTL 0 (permanent) or 1..1440 (RAM-only, expires).
- `DELETE|id`, `LIST`, `STATUS`, `PING`, `CLEAR`.
- Requests end with LF or CRLF. The receive limit is 107 bytes before the line ending. Empty or surplus fields are rejected.

## Replies

`ACK|seq|SAVED`, `ACK|seq|DELETED`, `ACK|seq|COUNT=n`, `ACK|seq|PONG`, `ACK|seq|CLEARED`; `LIST` emits zero or more `ITEM|seq|id|priority`, then `ACK|seq|END`.

`ERR|seq|ARG`, `FULL`, `NOT_FOUND`, `TEXT`, `UNKNOWN`, `CRC`, `OVERSIZE`, `FORMAT`.

**Important:** UART replies have no CRC in this teaching version; use command sequence numbers only for basic correlation. No automatic retries or exactly-once semantics are claimed. Android allows one outstanding request. A five-second timeout closes the connection and reports an unknown command outcome.

## Example

Payload `7|PING` yields CRC-8/ATM `0x6D` (see tests/protocol_reference.py to generate precise frames). On success board returns `ACK|7|PONG`.
