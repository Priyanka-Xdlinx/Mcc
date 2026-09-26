package com.orblinx.mcc.decode;

import java.util.Map;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Level 1 golden vectors.
 *
 * Every input here is real captured telemetry taken from the ground station's
 * own decode log, and every expected value is what the ground station printed
 * for those same bytes. Each case is chosen because a plausible wrong
 * implementation produces a different, plausible-looking answer.
 */
class ParameterDecoderTest {

    // AD-160 Raw CubeStar telemetry, bit-addressed, as logged by xdos_tm_view.
    private static final byte[] AD160 = hex(
            "1f 7f a0 00 f1 53 65 00 f1 53 65 00 00 08 09 90 5f 60 ea 30 75 00 00 06 03 00 00 00");
    // EP-016 Get_PDU_Voltage_Readings_RSP, byte-addressed.
    private static final byte[] EP016 = hex(
            "8b dc 6e 40 15 f4 6f 40 f1 8f 6f 40 39 df 6d 40 70 ad 6b 40");
    // EP-022 BMS_Get_Battery_Temp_Reading_RSP, byte-addressed.
    private static final byte[] EP022 = hex(
            "20 03 84 03 84 03 58 02 2c 01 00 00 00 00 64 00 f4 01 20 03 84 03 84 03");

    private final ParameterDecoder dec = new ParameterDecoder(Map.of(
            1, Map.of(0L, "Startup", 3L, "Aborted")));

    @Test
    @DisplayName("U1 EPS scaled INT: scale is a multiplier, not a divisor")
    void u1_scaledInt() {
        ParamDef p = byteParam("Temp Sense 1 Reading", 0, 2, "INT", false, 0.01, "°C");
        Object v = dec.decodeByteField(p, EP022, 0, 2);
        assertEquals(8.0, (Double) v, 1e-9, "0x0320 = 800, times 0.01");
        assertEquals("8.00 °C", dec.toValue(p, v, Quality.OK).formatted());
        // A divisor reading would give 80000 - four orders of magnitude out.
        assertNotEquals(80000.0, (Double) v);
    }

    @Test
    @DisplayName("U2 EPS FLOAT: IEEE-754 binary32, little-endian, no scale")
    void u2_float() {
        ParamDef p = byteParam("Conv1 OP Voltage Reading", 0, 4, "FLOAT", false, null, "V");
        Object v = dec.decodeByteField(p, EP016, 0, 4);
        assertEquals(3.7322109, (Double) v, 1e-6);
        assertEquals("3.732 V", dec.toValue(p, v, Quality.OK).formatted());
        // Big-endian would read -8.49e-32: wrong, but not obviously wrong.
        ParamDef be = byteParam("x", 0, 4, "FLOAT", true, null, "V");
        assertTrue(Math.abs((Double) dec.decodeByteField(be, EP016, 0, 4)) < 1e-30);
    }

    @Test
    @DisplayName("U3 ADCS 4-bit field at bit 68: packing is LSB-first")
    void u3_lsbFirst() {
        long raw = ParameterDecoder.getBits(AD160, 68, 4);
        assertEquals(15, raw, "byte 8 is 0xF1; bits 4..7 counted from the LSB are 0b1111");
        // MSB-first would take the high nibble the other way round and yield 1.
        assertNotEquals(1, raw);
    }

    @Test
    @DisplayName("U4 ADCS ENUM at bit 72 resolves through the subsystem's enum table")
    void u4_enum() {
        ParamDef p = bitParam("STR0 Identification Status", 72, 4, "ENUM", null, null, 1);
        Object v = dec.decodeBitField(p, ParameterDecoder.getBits(AD160, 72, 4));
        assertEquals("3 (Aborted)", dec.toValue(p, v, Quality.OK).formatted());
    }

    @Test
    @DisplayName("U5 ADCS single-bit BOOLs at bits 76..78")
    void u5_bools() {
        assertTrue((Boolean) dec.decodeBitField(bitParam("STR0 Idle", 76, 1, "BOOL", null, null, null),
                ParameterDecoder.getBits(AD160, 76, 1)));
        assertFalse((Boolean) dec.decodeBitField(bitParam("STR0 Tracking", 77, 1, "BOOL", null, null, null),
                ParameterDecoder.getBits(AD160, 77, 1)));
        assertTrue((Boolean) dec.decodeBitField(bitParam("STR0 valid", 78, 1, "BOOL", null, null, null),
                ParameterDecoder.getBits(AD160, 78, 1)));
    }

    @Test
    @DisplayName("U6 ADCS scaled INT in bit mode renders at 2dp, not 3")
    void u6_bitScaled() {
        ParamDef p = bitParam("STR0 Angular Rate X", 96, 16, "INT", 0.0001, null, null);
        Object v = dec.decodeBitField(p, ParameterDecoder.getBits(AD160, 96, 16));
        assertEquals(0.2048, (Double) v, 1e-9);
        // Declared INT, so it takes the scaled-integer format, not the float one.
        assertEquals("0.20", dec.toValue(p, v, Quality.OK).formatted());
    }

    @Test
    @DisplayName("U7 a 10-bit field spanning a byte boundary is read across both bytes")
    void u7_crossByte() {
        // Xmt0_Att sits at bit 873, width 10, i.e. bytes 109-110. Reading a single
        // container byte and shifting by (offset % 8) silently truncates it.
        byte[] buf = new byte[120];
        int off = 873, bits = 10, value = 0b1010101010;
        for (int i = 0; i < bits; i++) {
            if (((value >> i) & 1) != 0) {
                int bi = off + i;
                buf[bi >> 3] |= (byte) (1 << (bi & 7));
            }
        }
        assertEquals(value, ParameterDecoder.getBits(buf, off, bits));
        assertTrue((off / 8) != ((off + bits - 1) / 8), "the field must straddle two bytes");

        int naive = (buf[off / 8] & 0xFF) >> (off % 8);   // the single-byte read
        assertNotEquals(value, naive, "a one-byte container read must not happen to agree");
    }

    @Test
    @DisplayName("U9 HEX renders most-significant byte first whatever the wire order")
    void u9_hexOrder() {
        byte[] b = hex("01 02 03 04");
        ParamDef le = byteParam("k", 0, 4, "HEX", false, null, null);
        ParamDef be = byteParam("k", 0, 4, "HEX", true, null, null);
        assertEquals("0x04030201", dec.decodeByteField(le, b, 0, 4));
        assertEquals("0x01020304", dec.decodeByteField(be, b, 0, 4));
    }

    @Test
    @DisplayName("sign extension follows the declared width, not the container size")
    void signExtension() {
        assertEquals(-1, ParameterDecoder.signExtend(0xFFFF, 16));
        assertEquals(-1, ParameterDecoder.signExtend(0xF, 4));
        assertEquals(15, ParameterDecoder.signExtend(0xF, 8));
        assertEquals(2048, ParameterDecoder.signExtend(0x0800, 16));
    }

    @Test
    @DisplayName("fixed() prints the exact binary value and rounds half to even")
    void formatting() {
        assertEquals("-195352.312", ParameterDecoder.fixed(-195352.3125, 3));
        assertEquals("-0.000", ParameterDecoder.fixed(-1e-9, 3), "a small negative keeps its sign");
        assertEquals("-0.000", ParameterDecoder.fixed(-0.0, 3));
        assertEquals("0.000", ParameterDecoder.fixed(0.0, 3));
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    private static ParamDef byteParam(String name, int off, int len, String type,
                                      boolean bigEndian, Double scale, String unit) {
        return new ParamDef(1, 1, 1, name, off, len, off * 8, len * 8, true,
                            type, bigEndian, scale, unit, null, false);
    }

    private static ParamDef bitParam(String name, int off, int bits, String type,
                                     Double scale, String unit, Integer enumId) {
        return new ParamDef(1, 1, 1, name, off, bits, off, bits,
                            off % 8 == 0 && bits % 8 == 0, type, false, scale, unit, enumId, false);
    }

    private static byte[] hex(String s) {
        String[] parts = s.trim().split("\\s+");
        byte[] out = new byte[parts.length];
        for (int i = 0; i < parts.length; i++) out[i] = (byte) Integer.parseInt(parts[i], 16);
        return out;
    }
}
