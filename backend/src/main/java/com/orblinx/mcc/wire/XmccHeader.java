package com.orblinx.mcc.wire;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

/**
 * Parser for the fixed 26-byte XMCC v1 header. Every field is big-endian.
 *
 *   0 magic "XMCC" | 4 version | 5 rsv | 6 master frame u32 | 10 subframe u16
 *  12 year u16 | 14 month | 15 day | 16 hour | 17 min | 18 sec | 19 ms u16
 *  21 rsv | 22 payload length u32 | 26 payload
 *
 * The payload length is read from the header rather than assumed to be 1280:
 * it exists precisely so the feed stays parseable if the subframe size changes.
 */
public final class XmccHeader {

    public static final int LENGTH = 26;
    public static final int VERSION = 1;
    public static final byte[] MAGIC = {'X', 'M', 'C', 'C'};

    public final long masterFrame;
    public final int subframe;
    public final String spacecraftTime;
    public final int payloadLength;

    private XmccHeader(long mf, int sf, String time, int len) {
        this.masterFrame = mf;
        this.subframe = sf;
        this.spacecraftTime = time;
        this.payloadLength = len;
    }

    public static XmccHeader parse(byte[] h) {
        if (h.length < LENGTH) {
            throw new IllegalArgumentException("short header: " + h.length + " bytes");
        }
        for (int i = 0; i < 4; i++) {
            if (h[i] != MAGIC[i]) {
                throw new OutOfSyncException("bad magic: " + hex4(h));
            }
        }
        int version = h[4] & 0xFF;
        if (version != VERSION) {
            // A higher version means the GS was upgraded and the header may have
            // grown. Refuse rather than misparse it.
            throw new IllegalStateException(
                "unsupported XMCC version " + version + " - refusing to parse");
        }
        ByteBuffer b = ByteBuffer.wrap(h).order(ByteOrder.BIG_ENDIAN);
        long mf = Integer.toUnsignedLong(b.getInt(6));
        int sf = b.getShort(10) & 0xFFFF;
        int year = b.getShort(12) & 0xFFFF;
        int month = h[14] & 0xFF, day = h[15] & 0xFF;
        int hour = h[16] & 0xFF, min = h[17] & 0xFF, sec = h[18] & 0xFF;
        int ms = b.getShort(19) & 0xFFFF;
        long len = Integer.toUnsignedLong(b.getInt(22));
        if (len <= 0 || len > 1 << 20) {
            throw new OutOfSyncException("implausible payload length " + len);
        }
        String time = String.format("%04d-%02d-%02d %02d:%02d:%02d.%03d",
                                    year, month, day, hour, min, sec, ms);
        return new XmccHeader(mf, sf, time, (int) len);
    }

    private static String hex4(byte[] h) {
        StringBuilder s = new StringBuilder();
        for (int i = 0; i < 4; i++) s.append(String.format("%02x ", h[i]));
        return s.toString().trim();
    }

    /** Recoverable: scan forward for the next magic and resume. */
    public static class OutOfSyncException extends RuntimeException {
        public OutOfSyncException(String m) { super(m); }
    }
}
