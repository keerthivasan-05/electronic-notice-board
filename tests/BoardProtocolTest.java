package com.example.electronicnoticeboard;

import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;

public final class BoardProtocolTest {
    public static void main(String[] args) throws Exception {
        if (BoardProtocol.crc8("123456789".getBytes(StandardCharsets.US_ASCII)) != 0xF4)
            throw new AssertionError("CRC-8 test vector failed");
        String wire = BoardProtocol.encode(7, "PING");
        if (!wire.equals("@7|PING*6D\n")) throw new AssertionError(wire);
        BoardProtocol.Reply reply = BoardProtocol.parseReply("ACK|7|PONG");
        if (reply == null || reply.sequence != 7 || !reply.value.equals("PONG"))
            throw new AssertionError("reply parsing failed");
        String[] malformed = {
            "garbage", "ACK|+7|PONG", "ACK|7|PONG|extra", "ACK|7|",
            "ITEM|7|0|2", "ITEM|7|1|4", "ITEM|7|1", "ACK|7|PONG\r"
        };
        for (String line : malformed) {
            if (BoardProtocol.parseReply(line) != null) throw new AssertionError(line);
        }
        BoardProtocol.Reply item = BoardProtocol.parseReply("ITEM|255|1|3");
        if (item == null || item.isFinal() || !item.matches("LIST") || item.matches("PING"))
            throw new AssertionError("LIST item correlation failed");
        if (!reply.isFinal() || !reply.matches("PING") || reply.matches("CLEAR"))
            throw new AssertionError("ACK correlation failed");
        if (BoardProtocol.parseReply("ACK|7|COUNT=6").matches("STATUS"))
            throw new AssertionError("Out-of-range count accepted");
        BoardProtocol.encode(255, "ADD|1|2|0|hello@example.com");
        try { BoardProtocol.encode(1, "ADD|1|2|0|bad*data"); throw new AssertionError(); }
        catch (IllegalArgumentException expected) { }
        String stream = "X".repeat(200) + "\nACK|7|PONG\r\nITEM|7|1|3\nACK|7|END";
        BoardProtocol.ReplyReader reader = new BoardProtocol.ReplyReader(
                new ByteArrayInputStream(stream.getBytes(StandardCharsets.US_ASCII)));
        if (!reader.read().matches("PING") || !reader.read().matches("LIST") || reader.read() != null)
            throw new AssertionError("Stream framing failed");
        System.out.println("PASS: Java protocol tests");
    }
}
