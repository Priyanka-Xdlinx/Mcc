package com.orblinx.mcc.wire;

import java.io.DataInputStream;
import java.io.EOFException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Reads the ground station's XMCC feed off a TCP socket.
 *
 * The link is one-way: the GS only sends, and anything we write is ignored.
 * The GS accepts multiple concurrent clients, so this can run alongside the
 * Python reference decoder on the same feed - which is exactly how the two are
 * cross-checked live.
 *
 * TCP is a byte stream, not a message queue: one read may return half a message
 * or several joined together. Everything here loops until it has the exact
 * count it asked for.
 */
public final class XmccClient implements Runnable {

    private static final Logger log = LoggerFactory.getLogger(XmccClient.class);

    private final String host;
    private final int port;
    private final long reconnectDelayMs;
    private final Consumer<Subframe> sink;

    private final AtomicBoolean running = new AtomicBoolean(false);
    private final AtomicBoolean connected = new AtomicBoolean(false);
    private final AtomicLong received = new AtomicLong();
    private final AtomicLong rejected = new AtomicLong();
    private volatile Thread thread;
    private volatile Socket socket;

    public XmccClient(String host, int port, long reconnectDelayMs, Consumer<Subframe> sink) {
        this.host = host;
        this.port = port;
        this.reconnectDelayMs = reconnectDelayMs;
        this.sink = sink;
    }

    public void start() {
        if (!running.compareAndSet(false, true)) return;
        thread = new Thread(this, "xmcc-client");
        thread.setDaemon(true);
        thread.start();
    }

    public void stop() {
        running.set(false);
        closeQuietly();
        Thread t = thread;
        if (t != null) t.interrupt();
    }

    public boolean isConnected() { return connected.get(); }
    public long subframesReceived() { return received.get(); }
    /** Headers thrown away as out of sync (bad magic or implausible length). */
    public long headersRejected() { return rejected.get(); }

    @Override
    public void run() {
        while (running.get()) {
            try (Socket s = new Socket()) {
                socket = s;
                s.connect(new InetSocketAddress(host, port), 3000);
                s.setSoTimeout(30_000);
                connected.set(true);
                log.info("connected to XMCC feed {}:{}", host, port);
                consume(s.getInputStream());
            } catch (Exception e) {
                if (running.get()) {
                    log.warn("feed {}:{} unavailable ({}) - retrying", host, port,
                             e.getMessage());
                }
            } finally {
                connected.set(false);
                socket = null;
            }
            if (!running.get()) break;
            try {
                Thread.sleep(reconnectDelayMs);
            } catch (InterruptedException ie) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        log.info("XMCC client stopped after {} subframes", received.get());
    }

    private void consume(InputStream raw) throws Exception {
        DataInputStream in = new DataInputStream(raw);
        byte[] header = new byte[XmccHeader.LENGTH];
        // After a resync the buffer already holds a candidate header, so it must
        // be parsed rather than overwritten by the next read.
        boolean headerReady = false;
        while (running.get()) {
            if (!headerReady) in.readFully(header);
            headerReady = false;
            XmccHeader h;
            try {
                h = XmccHeader.parse(header);
            } catch (XmccHeader.OutOfSyncException oos) {
                rejected.incrementAndGet();
                log.warn("{} - resyncing on the next XMCC magic", oos.getMessage());
                resync(in, header);
                headerReady = true;
                continue;
            }
            byte[] payload = new byte[h.payloadLength];
            in.readFully(payload);
            received.incrementAndGet();
            sink.accept(new Subframe(h.masterFrame, h.subframe, h.spacecraftTime,
                                     header.clone(), payload));
        }
    }

    /**
     * Slide one byte at a time until the buffer starts with XMCC again. Framing
     * is recoverable; silently mis-parsing is not.
     */
    private void resync(DataInputStream in, byte[] buf) throws Exception {
        while (running.get()) {
            System.arraycopy(buf, 1, buf, 0, buf.length - 1);
            int b = in.read();
            if (b < 0) throw new EOFException("GS closed during resync");
            buf[buf.length - 1] = (byte) b;
            boolean ok = true;
            for (int i = 0; i < 4; i++) {
                if (buf[i] != XmccHeader.MAGIC[i]) { ok = false; break; }
            }
            if (ok) {
                // buf now holds a full candidate header starting at magic.
                return;
            }
        }
    }

    private void closeQuietly() {
        Socket s = socket;
        if (s != null) {
            try { s.close(); } catch (Exception ignored) { }
        }
    }
}
