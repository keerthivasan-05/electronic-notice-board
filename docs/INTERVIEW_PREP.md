# Interview preparation: smart Bluetooth electronic notice board

## 45-second elevator pitch

"My project is a Bluetooth-based smart notice board controlled by an Android Java application. I used an HC-05 Bluetooth Classic module as the wireless serial bridge and an Arduino Uno to manage the display. Instead of displaying one message directly, the firmware stores up to five notices, each with an ID, priority and optional expiry. The protocol uses framed commands with CRC-8 to detect transmission corruption. On the microcontroller, I used fixed-size buffers and a `millis()`-based nonblocking scheduler to process Bluetooth commands while scrolling notices. Permanent notices are saved in EEPROM, and I can query, delete or clear them from the phone. The most interesting challenges were protocol framing, MCU memory constraints, priority scheduling and reliable state restoration."

**Use this script only for parts you have actually implemented and tested yourself.** If you're studying the supplied reference implementation, say so; build it, change it and collect your own evidence before claiming ownership.

## Three-minute end-to-end explanation

1. The operator opens the Android app and pairs with an HC-05 Bluetooth module. The app displays bonded devices, requests runtime Bluetooth access as needed and connects using `BluetoothSocket` and the standard SPP service UUID.
2. The operator provides notice ID 1, priority 3, TTL 5 minutes and text `Seminar in Hall A`. The app validates these fields, produces `ADD|1|3|5|Seminar in Hall A`, prefixes a sequence value, calculates a CRC-8 and adds a newline terminator.
3. HC-05 presents these bytes on UART. The Arduino `SoftwareSerial` receiver accumulates bytes into a bounded buffer until it sees newline. It verifies the CRC before decoding the command and rejects oversized or malformed requests.
4. `NoticeStore` finds a slot, stores the notice and calculates expiry using current uptime. Permanent notices go to EEPROM; TTL-based messages stay in RAM.
5. Every 4 s, the scheduler selects the highest-priority active notice, using circular ordering among ties. Scrolling is updated every 300 ms with `millis()`, so the main event loop can still process other commands.
6. Arduino returns `ACK|sequence|SAVED` or a specific `ERR`, which is displayed in the mobile app. The operator can query `STATUS`, list IDs, delete a specific notice or clear the entire board.
7. On restart, permanent notices are restored by validating the EEPROM signature, version and checksum. Time-limited messages do not survive a restart because the Uno has no real-time clock.

## Questions and defensible answers

**Q1. Why is Bluetooth called a serial bridge here?** The Android side uses Bluetooth Classic RFCOMM / SPP, while the HC-05 exposes data through UART pins. The module converts between the two transports without interpreting the notice commands.

**Q2. Why not use Bluetooth Low Energy?** HC-05 is a Classic SPP device. BLE would require a BLE-capable module and GATT read/write characteristics instead of RFCOMM streams.

**Q3. What's the difference between UART and I2C?** UART is asynchronous point-to-point transmit/receive using a configured baud; I2C is clocked, addressing-capable communication using SDA and SCL. UART connects the Bluetooth bridge; I2C connects the LCD backpack.

**Q4. Why is newline framing needed?** RFCOMM is a byte stream; reads need not correspond to commands. A delimiter tells the parser when one complete application frame is ready.

**Q5. Why add CRC-8?** To detect accidental corruption of command bytes. CRC is not a cryptographic signature; it does not authenticate a sender.

**Q6. Why use a sequence number?** The controller echoes it in responses, allowing the app or a future request manager to match a reply with a command. This version logs responses but has no automated retry manager.

**Q7. Why use a fixed buffer instead of Arduino String?** Predictable RAM consumption and no runtime heap fragmentation in a small AVR microcontroller.

**Q8. Why no `delay(4000)`?** `delay()` would stop application processing during each page interval. Elapsed-time checks with `millis()` let the firmware poll Bluetooth, update timeouts and scroll cooperatively.

**Q9. What happens when two messages have priority 3?** The scheduler rotates among highest-priority notices in circular slot order, every 4 seconds.

**Q10. What happens to low-priority messages while a high-priority one exists?** They do not show. If fairness across priorities matters, weighted round robin should be implemented.

**Q11. Does the system store messages through a restart?** Only TTL=0 notices. Those are serialized into EEPROM; expiry notices are kept in RAM.

**Q12. Why does TTL reset when a device restarts?** This is avoided by dropping temporary records at reboot. Without an RTC, we cannot know how long the board was powered off.

**Q13. Is EEPROM unlimited?** No. It has limited write endurance, and power interruptions can cause incomplete saves. The demo uses `EEPROM.update()` and a checksum but is not a crash-atomic database.

**Q14. How does LCD scrolling work?** A 16-character window moves through an in-memory text buffer, with three spaces as a gap before the sequence repeats.

**Q15. What if a user sends 100 characters?** Android validates length before sending, while Arduino independently bounds the received frame and rejects oversized input. Never trust validation from just one side.

**Q16. What is the purpose of `volatile` in Java?** Here it makes socket/output references visible across the executor and receiver thread. It doesn't make multi-step connection lifecycle operations atomic.

**Q17. Why not write LCD operations from Bluetooth receive callback?** A single cooperative firmware loop keeps the state transitions more predictable. There is no multitask callback driver in this implementation.

**Q18. What is the hardest part?** Framing and state management under a small RAM constraint, because bytes may arrive incrementally and stale, expired or corrupted notices must not be displayed.

**Q19. Is it secure?** Not by itself. Pairing provides limited access control, but default HC-05 PINs can be weak. CRC gives error detection only, and no user roles or cryptographic authorization are implemented.

**Q20. What would you build next?** An ESP32/BLE version, role-based authenticated admin app, NTP/RTC synchronized scheduling, dual-bank persistence, optional cloud fallback, and an LED matrix display.

## Live-demo narrative

- Start with empty board; PING -> PONG.
- Add ID 1 at priority 1; watch display change.
- Add ID 2 at priority 3 and TTL=1 min; see preemption.
- LIST and STATUS -> show state.
- Wait for expiry; see ID 1 resume.
- Reboot board; ID 1 (permanent) remains, ID 2 (temporary) does not.
- Trigger invalid CRC using a USB-UART testing setup, inspect `ERR|0|CRC`.

## Resume entry (only after personally implementing)

**Smart Electronic Notice Board | Java, Embedded C++, Arduino, Bluetooth SPP, I2C, EEPROM**

"Developed an Android-controlled Arduino notice display with a framed CRC-8 command protocol, priority-aware scheduling for five notices, TTL-based expiry and EEPROM persistence for permanent entries. Implemented bounded UART parsing, nonblocking LCD scrolling, Bluetooth status acknowledgements and CRUD operations."

Do not state a measured reliability improvement, response latency, battery life, security property, or field deployment without data.
