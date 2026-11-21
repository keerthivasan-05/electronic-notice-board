"""Run: python3 tests/protocol_reference.py (no third-party packages)."""

def crc8(data: bytes) -> int:
    crc = 0
    for value in data:
        crc ^= value
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xff if crc & 0x80 else (crc << 1) & 0xff
    return crc


def frame(sequence: int, command: str) -> bytes:
    assert 0 <= sequence <= 255
    assert all(32 <= ord(c) <= 126 for c in command)
    payload = f"{sequence}|{command}".encode("ascii")
    return b"@" + payload + f"*{crc8(payload):02X}\n".encode("ascii")


def parse(request: bytes):
    if not request.startswith(b"@") or not request.endswith(b"\n"):
        raise ValueError("bad framing")
    payload, provided = request[1:-1].rsplit(b"*", 1)
    if crc8(payload) != int(provided, 16):
        raise ValueError("bad CRC")
    return payload.decode("ascii").split("|")


def tests():
    examples = ["PING", "STATUS", "LIST", "ADD|3|2|10|Exam starts at 9AM", "DELETE|3", "CLEAR"]
    for i, cmd in enumerate(examples):
        wire = frame(i, cmd)
        assert parse(wire) == [str(i)] + cmd.split("|")
        print(wire.decode("ascii").strip())
    bad = bytearray(frame(7, "PING")); bad[3] ^= 1
    try:
        parse(bytes(bad))
    except ValueError:
        pass
    else:
        raise AssertionError("Corruption undetected")
    assert crc8(b"123456789") == 0xF4  # canonical CRC-8/ATM check value
    print("PASS: reference frame tests")

if __name__ == "__main__":
    tests()
