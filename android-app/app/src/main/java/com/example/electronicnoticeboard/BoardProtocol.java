package com.example.electronicnoticeboard;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

/** Pure Java protocol implementation; independent of Android APIs and unit-testable. */
public final class BoardProtocol {
    static final int NOTICE_CAPACITY = 5;
    private static final int MAX_REPLY_LENGTH = 96;
    private static final int MAX_REQUEST_LENGTH = 107;

    private BoardProtocol() {}
    public static int crc8(byte[] data) {
        int crc = 0;
        for (byte value : data) {
            crc ^= value & 0xff;
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x80) != 0 ? ((crc << 1) ^ 0x07) & 0xff : (crc << 1) & 0xff;
        }
        return crc;
    }
    public static String encode(int sequence, String command) {
        if (sequence < 0 || sequence > 255 || command == null || command.isEmpty())
            throw new IllegalArgumentException("Invalid sequence or command");
        for (int i = 0; i < command.length(); i++) {
            char c = command.charAt(i);
            if (c < 32 || c > 126 || c == '*')
                throw new IllegalArgumentException("Command contains reserved character");
        }
        String payload = sequence + "|" + command;
        if (payload.length() + 5 > MAX_REQUEST_LENGTH)
            throw new IllegalArgumentException("Frame exceeds Arduino receive limit");
        return "@" + payload + "*" + String.format(Locale.US, "%02X",
                crc8(payload.getBytes(StandardCharsets.US_ASCII))) + "\n";
    }
    public static final class Reply {
        public final String type;
        public final int sequence;
        public final String value;
        private Reply(String type, int sequence, String value) {
            this.type = type;
            this.sequence = sequence;
            this.value = value;
        }
        public boolean isFinal() { return !"ITEM".equals(type); }

        public boolean matches(String command) {
            if ("ERR".equals(type)) return true;
            if ("ITEM".equals(type)) return "LIST".equals(command);
            if (command.startsWith("ADD|")) return "SAVED".equals(value);
            if (command.startsWith("DELETE|")) return "DELETED".equals(value);
            switch (command) {
                case "PING": return "PONG".equals(value);
                case "CLEAR": return "CLEARED".equals(value);
                case "LIST": return "END".equals(value);
                case "STATUS": return value.startsWith("COUNT=") &&
                        decimal(value.substring(6), 0, NOTICE_CAPACITY) >= 0;
                default: return false;
            }
        }
    }

    public static Reply parseReply(String line) {
        if (line == null || line.length() > MAX_REPLY_LENGTH) return null;
        for (int i = 0; i < line.length(); i++) {
            char c = line.charAt(i);
            if (c < 32 || c > 126) return null;
        }

        String[] fields = line.split("\\|", -1);
        if (fields.length < 3) return null;
        int sequence = decimal(fields[1], 0, 255);
        if (sequence < 0) return null;
        switch (fields[0]) {
            case "ACK":
            case "ERR":
                if (fields.length == 3 && !fields[2].isEmpty())
                    return new Reply(fields[0], sequence, fields[2]);
                break;
            case "ITEM":
                if (fields.length == 4 && decimal(fields[2], 1, 255) >= 0 &&
                        decimal(fields[3], 1, 3) >= 0)
                    return new Reply(fields[0], sequence, fields[2] + "|" + fields[3]);
                break;
            default:
                break;
        }
        return null;
    }

    /** Reads complete replies and discards invalid or oversized lines through the next LF. */
    static final class ReplyReader {
        private final InputStream input;
        private final StringBuilder line = new StringBuilder(MAX_REPLY_LENGTH + 1);

        ReplyReader(InputStream input) {
            this.input = input;
        }

        Reply read() throws IOException {
            boolean overflow = false;
            int value;
            while ((value = input.read()) != -1) {
                if (value == '\n') {
                    Reply reply = overflow ? null : parseLine();
                    line.setLength(0);
                    overflow = false;
                    if (reply != null) return reply;
                } else if (!overflow) {
                    if (line.length() == MAX_REPLY_LENGTH + 1) overflow = true;
                    else line.append((char) value);
                }
            }
            line.setLength(0);
            return null;
        }

        private Reply parseLine() {
            int length = line.length();
            if (length > 0 && line.charAt(length - 1) == '\r') line.setLength(length - 1);
            return parseReply(line.toString());
        }
    }

    private static int decimal(String field, int min, int max) {
        if (field.isEmpty() || field.length() > 3) return -1;
        int value = 0;
        for (int i = 0; i < field.length(); i++) {
            char c = field.charAt(i);
            if (c < '0' || c > '9') return -1;
            value = value * 10 + c - '0';
        }
        return value >= min && value <= max ? value : -1;
    }
}
