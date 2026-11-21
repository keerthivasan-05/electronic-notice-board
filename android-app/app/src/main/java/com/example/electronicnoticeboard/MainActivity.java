package com.example.electronicnoticeboard;

import android.Manifest;
import android.app.AlertDialog;
import android.app.Activity;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothManager;
import android.bluetooth.BluetoothSocket;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.widget.ArrayAdapter;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.Spinner;
import android.widget.TextView;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public class MainActivity extends Activity {
    private static final long REQUEST_TIMEOUT_MS = 5000;
    private static final UUID SPP = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB");
    private final ExecutorService io = Executors.newSingleThreadExecutor();
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ArrayList<BluetoothDevice> devices = new ArrayList<>();
    private BluetoothAdapter adapter;
    private final Object connectionLock = new Object();
    // Guarded by connectionLock, including publication of sockets still connecting.
    private Connection connection;
    private long connectionVersion;

    private static final class Connection {
        final BluetoothSocket socket;
        OutputStream output;
        Request pending;
        int nextSequence = 1; // Zero is reserved for uncorrelated firmware errors.

        Connection(BluetoothSocket socket) {
            this.socket = socket;
        }
    }

    private static final class Request {
        final int sequence;
        final String command;
        int itemsReceived;

        Request(int sequence, String command) {
            this.sequence = sequence;
            this.command = command;
        }
    }
    private Spinner deviceSelector, prioritySelector;
    private EditText idInput, ttlInput, messageInput;
    private TextView log;
    private Button addButton;
    private volatile boolean destroyed = false;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        buildUi();
        BluetoothManager manager = (BluetoothManager) getSystemService(BLUETOOTH_SERVICE);
        adapter = manager == null ? null : manager.getAdapter();
        if (adapter == null) append("Bluetooth is unavailable on this phone.");
        else refresh();
    }

    private void append(String s) {
        main.post(() -> {
            if (destroyed || log == null) return;
            String old = log.getText().toString();
            log.setText(s + "\n" + old.substring(0, Math.min(old.length(), 5000)));
        });
    }
    private EditText field(LinearLayout root, String hint, String initial) {
        EditText value = new EditText(this);
        value.setHint(hint);
        value.setSingleLine(true);
        value.setText(initial);
        root.addView(value);
        return value;
    }
    private Button button(LinearLayout root, String text, Runnable click) {
        Button b = new Button(this);
        b.setText(text);
        b.setOnClickListener(v -> click.run());
        root.addView(b);
        return b;
    }
    private void buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setPadding(24, 32, 24, 24);
        TextView title = new TextView(this);
        title.setText("Smart Electronic Notice Board");
        title.setTextSize(23);
        root.addView(title);
        deviceSelector = new Spinner(this);
        root.addView(deviceSelector);
        button(root, "Refresh paired devices", this::refresh);
        button(root, "Connect HC-05", this::connect);
        idInput = field(root, "Notice ID (1-255)", "1");
        prioritySelector = new Spinner(this);
        ArrayAdapter<String> priorities = new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_item,
                new String[]{"1 - Low", "2 - Normal", "3 - Urgent"});
        priorities.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item);
        prioritySelector.setAdapter(priorities);
        prioritySelector.setSelection(1);
        root.addView(prioritySelector);
        ttlInput = field(root, "Expiry in minutes, 0 = permanent", "0");
        messageInput = field(root, "Notice text (max 64 ASCII characters)", "Welcome to our campus!");
        addButton = button(root, "Add / Update Notice", this::add);
        addButton.setEnabled(false);
        button(root, "Delete ID", () -> {
            Integer id = readNumber(idInput, 1, 255);
            if (id != null) send("DELETE|" + id);
        });
        button(root, "List notices", () -> send("LIST"));
        button(root, "Board status", () -> send("STATUS"));
        button(root, "Connection ping", () -> send("PING"));
        button(root, "Clear all notices", () -> new AlertDialog.Builder(this)
                .setMessage("Remove every notice from the board?")
                .setNegativeButton("Cancel", null)
                .setPositiveButton("Clear", (dialog, which) -> send("CLEAR"))
                .show());
        button(root, "Disconnect", this::disconnect);
        log = new TextView(this);
        log.setText("Pair HC-05 in Android Bluetooth settings first.\n");
        root.addView(log);
        ScrollView scroll = new ScrollView(this);
        scroll.addView(root);
        setContentView(scroll);
    }
    private boolean permission() {
        return Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                checkSelfPermission(Manifest.permission.BLUETOOTH_CONNECT) == PackageManager.PERMISSION_GRANTED;
    }
    private void refresh() {
        if (adapter == null) return;
        if (!permission()) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S)
                requestPermissions(new String[]{Manifest.permission.BLUETOOTH_CONNECT}, 10);
            return;
        }
        if (!adapter.isEnabled()) {
            startActivityForResult(new Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE), 11);
            return;
        }
        devices.clear();
        ArrayList<String> labels = new ArrayList<>();
        Set<BluetoothDevice> bonded = adapter.getBondedDevices();
        for (BluetoothDevice d : bonded) {
            devices.add(d);
            labels.add((d.getName() == null ? "Unknown" : d.getName()) + " — " + d.getAddress());
        }
        deviceSelector.setAdapter(new ArrayAdapter<>(this,
                android.R.layout.simple_spinner_dropdown_item, labels));
        append("Paired devices: " + devices.size());
    }
    private void connect() {
        if (!permission()) { refresh(); return; }
        int index = deviceSelector.getSelectedItemPosition();
        if (index < 0 || index >= devices.size()) { append("Select HC-05 first."); return; }
        BluetoothDevice device = devices.get(index);
        disconnect();
        final long attemptVersion;
        synchronized (connectionLock) {
            attemptVersion = connectionVersion;
        }
        io.execute(() -> connectDevice(device, attemptVersion));
    }

    private void connectDevice(BluetoothDevice device, long attemptVersion) {
        BluetoothSocket candidate = null;
        Connection attempt = null;
        try {
            if (destroyed || cancelled(attemptVersion)) return;
            candidate = device.createRfcommSocketToServiceRecord(SPP);
            synchronized (connectionLock) {
                if (destroyed || connectionVersion != attemptVersion) return;
                attempt = new Connection(candidate);
                connection = attempt;
            }
            candidate.connect();
            OutputStream stream = candidate.getOutputStream();
            synchronized (connectionLock) {
                if (connection != attempt) return;
                attempt.output = stream;
            }
            updateControls();
            append("Connected to " + device.getAddress());
            Connection connected = attempt;
            new Thread(() -> readReplies(connected), "board-replies").start();
            candidate = null; // Reader now owns socket cleanup.
        } catch (IOException | SecurityException e) {
            if (attempt == null ? !cancelled(attemptVersion) : detach(attempt)) append("Connect failed: " + e.getMessage());
        } finally {
            close(candidate);
        }
    }

    private boolean cancelled(long attemptVersion) {
        synchronized (connectionLock) {
            return connectionVersion != attemptVersion;
        }
    }

    private void readReplies(Connection connected) {
        String outcome = "Board disconnected.";
        try (InputStream input = new BufferedInputStream(connected.socket.getInputStream())) {
            BoardProtocol.ReplyReader reader = new BoardProtocol.ReplyReader(input);
            BoardProtocol.Reply reply;
            while ((reply = reader.read()) != null) handleReply(connected, reply);
        } catch (IOException e) {
            outcome = "Link lost: " + e.getMessage();
        } finally {
            if (detach(connected)) append(outcome);
            close(connected.socket);
        }
    }

    private void handleReply(Connection connected, BoardProtocol.Reply reply) {
        synchronized (connectionLock) {
            Request request = connected.pending;
            if (connection != connected || request == null || request.sequence != reply.sequence ||
                    !reply.matches(request.command)) return;
            if (reply.isFinal()) {
                connected.pending = null;
            } else {
                if (request.itemsReceived == BoardProtocol.NOTICE_CAPACITY) return;
                ++request.itemsReceived;
            }
            append("Board [" + request.command + "] > " + reply.type + "|" + reply.sequence + "|" + reply.value);
        }
    }

    private Integer readNumber(EditText field, int min, int max) {
        try {
            int n = Integer.parseInt(field.getText().toString().trim());
            if (n >= min && n <= max) return n;
        } catch (NumberFormatException ignored) { }
        append("Invalid number (expected " + min + ".." + max + ")");
        return null;
    }
    private void add() {
        Integer id = readNumber(idInput, 1, 255);
        Integer ttl = readNumber(ttlInput, 0, 1440);
        String message = messageInput.getText().toString().trim();
        if (id == null || ttl == null) return;
        if (message.isEmpty() || message.length() > 64) {
            append("Message must be 1..64 characters."); return;
        }
        for (int i = 0; i < message.length(); i++) {
            char c = message.charAt(i);
            if (c < 32 || c > 126 || c == '|' || c == '*') {
                append("Use printable ASCII; '|' and '*' are reserved."); return;
            }
        }
        send("ADD|" + id + "|" + (prioritySelector.getSelectedItemPosition() + 1)
                + "|" + ttl + "|" + message);
    }
    private void send(String command) {
        final Connection connected;
        final Request request;
        final byte[] frame;
        synchronized (connectionLock) {
            connected = connection;
            if (connected == null || connected.output == null) {
                append("Not connected.");
                return;
            }
            if (connected.pending != null) {
                append("Wait for the current command to finish.");
                return;
            }
            request = new Request(connected.nextSequence, command);
            connected.nextSequence = connected.nextSequence == 255 ? 1 : connected.nextSequence + 1;
            frame = BoardProtocol.encode(request.sequence, command).getBytes(StandardCharsets.US_ASCII);
            connected.pending = request;
        }
        main.postDelayed(() -> timeout(connected, request), REQUEST_TIMEOUT_MS);
        io.execute(() -> {
            synchronized (connectionLock) {
                if (connection != connected || connected.pending != request) return;
            }
            try {
                connected.output.write(frame);
                connected.output.flush();
            } catch (IOException e) {
                if (detach(connected)) {
                    close(connected.socket);
                    append("Send failed: " + e.getMessage());
                }
            }
        });
    }

    private void timeout(Connection connected, Request request) {
        synchronized (connectionLock) {
            if (connection != connected || connected.pending != request) return;
            connection = null;
        }
        close(connected.socket);
        updateControls();
        append("Timed out waiting for " + request.command + ". Its outcome is unknown. Reconnect to continue.");
    }

    private static void close(BluetoothSocket socket) {
        if (socket != null) {
            try {
                socket.close();
            } catch (IOException ignored) {
                // Closing is best effort after the connection has been detached.
            }
        }
    }

    private boolean detach(Connection expected) {
        synchronized (connectionLock) {
            if (connection != expected) return false;
            connection = null;
        }
        updateControls();
        return true;
    }

    private void updateControls() {
        main.post(() -> {
            synchronized (connectionLock) {
                if (!destroyed) addButton.setEnabled(connection != null && connection.output != null);
            }
        });
    }

    private void disconnect() {
        final Connection old;
        synchronized (connectionLock) {
            ++connectionVersion;
            old = connection;
            connection = null;
        }
        if (old != null) close(old.socket);
        updateControls();
    }

    @Override protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == 11 && resultCode == RESULT_OK) refresh();
    }

    @Override public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        if (requestCode == 10 && results.length > 0 && results[0] == PackageManager.PERMISSION_GRANTED) refresh();
    }
    @Override protected void onDestroy() {
        destroyed = true;
        disconnect();
        main.removeCallbacksAndMessages(null);
        io.shutdownNow();
        super.onDestroy();
    }
}
