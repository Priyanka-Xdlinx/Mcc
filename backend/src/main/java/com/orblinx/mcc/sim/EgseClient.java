package com.orblinx.mcc.sim;

import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.LinkedHashMap;
import java.util.Map;

/**
 * The EGSE umbilical on the EPS simulator.
 *
 * This is the bench connection a technician uses before the spacecraft is on:
 * it holds the kill switch, and reports EPS and OBC state. Nothing reaches the
 * telemetry feed until the kill switch is released here — the OBC boots
 * unpowered, the rate-group engine never polls, and port 9100 stays silent.
 *
 * Wire format (src/common/egse.{hpp,cpp}): 'X','G','S','E', cmd, len, payload…
 * with 16-bit fields little-endian.
 */
public final class EgseClient {

    private static final byte[] MAGIC = {'X', 'G', 'S', 'E'};

    private static final byte CMD_EPS_STATUS = 0x01;
    private static final byte CMD_KILL_SET   = 0x02;
    private static final byte CMD_OBC_STATUS = 0x11;
    private static final byte REP_EPS_STATUS = (byte) 0x81;
    private static final byte REP_KILL_SET   = (byte) 0x82;
    private static final byte REP_OBC_STATUS = (byte) 0x91;

    private static final String[] OBC_STATE = {"Unpowered", "Booting", "Sequencing", "On"};
    /** Database EpsMode 0..4. */
    private static final String[] EPS_MODE =
            {"All OP Off", "Critical", "Safe", "Normal", "Full"};

    private final String host;
    private final int port;

    public EgseClient(String host, int port) {
        this.host = host;
        this.port = port;
    }

    /** Release (true) or re-assert (false) the kill switch. Returns the simulator's ack. */
    public boolean setKillSwitch(boolean on) throws Exception {
        byte[] reply = exchange(frame(CMD_KILL_SET, new byte[]{(byte) (on ? 1 : 0)}));
        return reply.length >= 7 && reply[4] == REP_KILL_SET && reply[6] != 0;
    }

    /** EPS and OBC state, or an empty map when the umbilical is unreachable. */
    public Map<String, Object> status() {
        Map<String, Object> m = new LinkedHashMap<>();
        try {
            byte[] eps = exchange(frame(CMD_EPS_STATUS, new byte[0]));
            if (eps.length >= 18 && eps[4] == REP_EPS_STATUS) {
                int o = 6;
                m.put("killReleased", eps[o++] != 0);
                int mode = eps[o++] & 0xFF;
                m.put("epsMode", mode);
                m.put("epsModeName", mode < EPS_MODE.length ? EPS_MODE[mode] : "?");
                int phase = eps[o++] & 0xFF;
                m.put("phase", phase == 0 ? "sunlit" : "eclipse");
                m.put("phaseRemainingS", u16(eps, o)); o += 2;
                m.put("battMv", u16(eps, o)); o += 2;
                m.put("socPct", eps[o++] & 0xFF);
                m.put("channelMask", u16(eps, o)); o += 2;
                m.put("shedMask", u16(eps, o));
            }
        } catch (Exception ignored) { }
        try {
            byte[] obc = exchange(frame(CMD_OBC_STATUS, new byte[0]));
            if (obc.length >= 10 && obc[4] == REP_OBC_STATUS) {
                int state = obc[6] & 0xFF;
                m.put("obcState", state < OBC_STATE.length ? OBC_STATE[state] : "?");
                m.put("obcCountdownS", u16(obc, 7));
                m.put("obcSeqDone", obc[9] & 0xFF);
            }
        } catch (Exception ignored) { }
        return m;
    }

    public boolean reachable() {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), 500);
            return true;
        } catch (Exception e) {
            return false;
        }
    }

    // ── wire ────────────────────────────────────────────────────────────────

    private static byte[] frame(byte cmd, byte[] payload) {
        byte[] f = new byte[6 + payload.length];
        System.arraycopy(MAGIC, 0, f, 0, 4);
        f[4] = cmd;
        f[5] = (byte) payload.length;
        System.arraycopy(payload, 0, f, 6, payload.length);
        return f;
    }

    private static int u16(byte[] b, int off) {
        return (b[off] & 0xFF) | ((b[off + 1] & 0xFF) << 8);
    }

    private byte[] exchange(byte[] request) throws Exception {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), 1500);
            s.setSoTimeout(1500);
            OutputStream out = s.getOutputStream();
            out.write(request);
            out.flush();

            InputStream in = s.getInputStream();
            byte[] head = readExact(in, 6);
            int len = head[5] & 0xFF;
            byte[] full = new byte[6 + len];
            System.arraycopy(head, 0, full, 0, 6);
            if (len > 0) System.arraycopy(readExact(in, len), 0, full, 6, len);
            return full;
        }
    }

    private static byte[] readExact(InputStream in, int n) throws Exception {
        byte[] buf = new byte[n];
        int got = 0;
        while (got < n) {
            int r = in.read(buf, got, n - got);
            if (r < 0) throw new java.io.EOFException("EGSE closed after " + got + " bytes");
            got += r;
        }
        return buf;
    }
}
