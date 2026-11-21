# Smart Electronic Notice Board — Intermediate Embedded + Android Project

**Stack:** Android Java, Bluetooth Classic SPP, HC-05, Arduino Uno (ATmega328P), Embedded C++, I2C, 16×2 LCD, EEPROM, CRC-8, Python reference testing.

## Project abstract

A mobile-managed electronic notice system for classrooms, laboratories or offices. An authenticated-by-pairing Android device sends framed commands over Bluetooth Classic to an Arduino-based embedded controller. Arduino checks integrity and syntax, stores up to five notices (IDs 1–255), rotates the highest-priority notices, scrolls lengthy text on a 16×2 LCD, expires temporary notices, and persists permanent notices to onboard EEPROM. The phone can add/update/delete notices, retrieve IDs/priority, request status, and clear the board. **Not suitable for safety-critical public announcements without hardening.**

## System architecture

```text
Android UI (Java)
   -> command validation + CRC-8/ATM encoder
   -> BluetoothSocket RFCOMM (SPP), ASCII bytes
   -> HC-05 serial bridge (9600 bps)
   -> SoftwareSerial (UNO D10 RX / D11 TX)
   -> bounded line accumulator (frame parser)
   -> CRC + request dispatcher
   -> NoticeStore (5 slots, IDs, priorities, deadlines)
      -> EEPROM (permanent notices only)
   -> nonblocking scheduler (millis)
   -> Wire / I2C -> LCD 16x2
   <- ACK / ERR / ITEM responses over same Bluetooth link
```

The app only sends **commands**, not screen pixels. The MCU maintains its own notice state and updates the display when the phone disconnects.

## Feature matrix

| Feature | Behavior |
|---|---|
| Add/update by ID | same ID replaces the previous entry |
| Delete | removes by ID; response is NOT_FOUND for unknown IDs |
| Priority | 3 > 2 > 1; the highest active class is shown |
| Fairness | round robin **within** a priority class; lower classes can starve while higher are present |
| Expiry | TTL minutes; 0 = permanent, 1–1440 = RAM-only expiry |
| Persistence | EEPROM snapshot for permanent notices only; temporary notices disappear on reboot |
| Integrity | request CRC-8/ATM; detects typical accidental corruption, not malicious tampering |
| Observability | STATUS, LIST, PING, serial debugging |
| LCD | 16 columns × 2 rows, fixed first row, scrolling second row |
| Memory discipline | fixed arrays and bounded UART buffers on MCU |

## Hardware bill of materials

- Arduino Uno / compatible ATmega328P board
- HC-05 Bluetooth Classic serial module **with suitable breakout/regulator**
- 16x2 HD44780-compatible LCD with PCF8574 I2C backpack (commonly address `0x27`, alternatively `0x3F`)
- Breadboard, jumpers, USB cable, resistor divider **1 kΩ + 2 kΩ** for UNO TX to HC-05 RX (verify module input rating)
- Android phone supporting Bluetooth Classic and a working Android Studio environment

## Pin mapping

| Arduino Uno | Peripheral | Notes |
|---|---|---|
| 5V | LCD VCC | Check LCD module rating |
| GND | LCD GND | Common ground with HC-05 |
| A4 / SDA | LCD SDA | I2C data |
| A5 / SCL | LCD SCL | I2C clock |
| D10 (RX) | HC-05 TXD | 3.3 V logic typically reads HIGH on Uno |
| D11 (TX) | HC-05 RXD via divider | Never blindly feed 5 V into 3.3 V-only input |
| 5V / 3.3V | HC-05 VCC | **Breakout-dependent**: regulated breakout may accept 5V, bare HC-05 needs regulated 3.3V. Check your board. |
| GND | HC-05 GND | Mandatory common ground |

Divider example: UNO D11 -> 1 kΩ -> junction -> HC-05 RXD; junction -> 2 kΩ -> GND; 5V output becomes approximately 3.33 V. Disconnect power while wiring.

## How to run

1. Install Arduino IDE. Install `LiquidCrystal_I2C` library variant with `lcd.init()` / `lcd.backlight()` API. `Wire`, `EEPROM`, `SoftwareSerial` come with the AVR environment. Libraries with a different constructor or API may need an adaptation.
2. Wire hardware as above. Inspect I2C backpack address and HC-05 data-mode baud (frequently 9600). Change code if yours differs.
3. Open **firmware/SmartNoticeBoard/SmartNoticeBoard.ino** as the Arduino sketch; `.cpp` and `.h` must stay in the same sketch directory. Select **Arduino Uno** and upload through USB.
4. Build **android-app/** as a Gradle project in Android Studio. It uses Java 17 and AGP 8.7.3 (check Android Studio/JDK compatibility; local SDK installation is needed). Pair the phone with HC-05 from Android Settings (often default PIN `1234` or `0000`; change pairing security if module supports it).
5. Open the app, grant Bluetooth permission, refresh paired devices, select HC-05 and tap Connect. Android 12+ requires `BLUETOOTH_CONNECT`; Android 11 and older use legacy manifest Bluetooth permission.
6. Set ID 1, priority 2, TTL 0, text `Welcome to the lab`, tap **Add / Update Notice**. The LCD should show title on top and notice beneath, and app should receive `ACK|seq|SAVED`.
7. Add ID 2, priority 3, TTL 1, text `Exam starts at 9AM`. Urgent notice should supersede normal notice, then expire after about 60 s.
8. Try **List notices**, **Board status**, **Delete ID**, and reboot MCU to confirm only TTL=0 items return.
9. Test reference protocol locally: `python3 tests/protocol_reference.py`.

## Directory structure

```text
ElectronicNoticeBoard_Intermediate/
├── android-app/                 Android Studio Gradle app (Java)
├── firmware/SmartNoticeBoard/
│   ├── SmartNoticeBoard.ino     UART parser, dispatch, scheduler, LCD
│   ├── NoticeStore.h           data model and storage API
│   └── NoticeStore.cpp         CRUD, expiry, CRC-protected EEPROM snapshot
├── docs/
│   ├── PROTOCOL.md             wire protocol and replies
│   ├── TECHNICAL_WALKTHROUGH.md architecture and trade-offs
│   ├── INTERVIEW_PREP.md       presentation, Q&A, honest resume claims
│   └── TEST_PLAN.md           tests, expected outcomes, limitations
└── tests/                      Python, Java and host C++ regression tests
```

## Important implementation constraints

- **Never claim encryption** based on CRC. CRC detects corruption; Bluetooth pairing/SPP trust is not application-level authorization. The standard HC-05 does not provide the security of a modern authenticated and encrypted backend.
- `STATUS` is active notice *count*, not memory health or message-delivery certainty.
- A 16x2 character LCD is only 16 visible characters per line; scrolling does not increase the hardware resolution.
- Priority 3 traffic can prevent priority 1/2 from appearing until it expires/is deleted; this is explicit scheduling policy, not a bug.
- `millis()` TTL is relative to board uptime; it does not mean calendar time, and temporary notices do not survive resets.
- EEPROM uses two CRC-checked banks. The firmware keeps the previous snapshot available until it commits the new one.
- Android correlates one in-flight request per connection. After five seconds without a final reply, it disconnects and reports that the command outcome is unknown.
- Automated checks cover the protocol and simulated firmware. Use the hardware test plan to verify the physical board and Android app together.

## Interview positioning

Call the project: **“Bluetooth-Based Priority-Aware Smart Electronic Notice Board with Integrity Validation and EEPROM Persistence.”** Explain why the MCU owns the scheduler, how CRC and bounded buffers work, and which production improvements you would make. Never claim implementation of cloud sync, biometrics, authentication, full delivery guarantees, or production-grade persistence.


## SWE-level code upgrade (v2.1)

See [docs/SWE_ENGINEERING_UPGRADE.md](docs/SWE_ENGINEERING_UPGRADE.md) for the architecture and limitations.
Protocol implementation is extracted to `android-app/.../BoardProtocol.java`, with Java tests.
Run `bash tests/run_tests.sh` (requires JDK 17, Python 3, and a C++11 compiler).
Firmware EEPROM writes are skipped for temporary-only changes and identical permanent snapshots.
Android tracks command sequence IDs, correlates responses and times out requests after five seconds.
