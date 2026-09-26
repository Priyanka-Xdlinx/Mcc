package com.orblinx.mcc.decode;

import java.math.BigDecimal;
import java.math.RoundingMode;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Turns tier-block bytes into named engineering values.
 *
 * This is an independent re-implementation of the ground station's own decoder
 * (src/codec/codec.cpp), field rule for field rule. It is deliberately NOT a
 * translation of any intermediate tool, so that agreement between the two is
 * evidence rather than tautology.
 *
 * Three rules are easy to get wrong and are the reason most of this class
 * exists:
 *
 *   1. ADCS and XLINK-S are bit-addressed and packed LSB-first, across the
 *      whole payload - not within a single container byte. 24% of the
 *      downlinked parameters are not byte-aligned and some span a byte
 *      boundary, so a one-byte container read is wrong for them.
 *   2. `scale` is a multiplier on decode (the encoder divides by it).
 *   3. All-0xFF across a field means the subsystem did not answer. It is not a
 *      reading of 255, and it is decided per parameter, not per message.
 */
public final class ParameterDecoder {

    /** enum_type_id -> (raw value -> label) */
    private final Map<Integer, Map<Long, String>> enums;

    public ParameterDecoder(Map<Integer, Map<Long, String>> enums) {
        this.enums = enums;
    }

    // ── entry-level decode ──────────────────────────────────────────────────

    /**
     * Decode one lookup-table entry out of an assembled tier block.
     *
     * Each pick is read at its own ABSOLUTE {@code block} offset and scattered
     * back to the position it occupied in the original subsystem reply, so the
     * MIB's offsets apply unchanged. Slicing the block sequentially by entry
     * would only work while entries happen to be laid out contiguously.
     */
    public List<DecodedValue> decodeEntry(EntryDef entry, MessageDef msg,
                                          byte[] block, Quality blockQuality) {
        return decodeEntry(entry, msg, block, blockQuality, null, null);
    }

    /**
     * The same decode, optionally recording what it did.
     *
     * When {@code trace} is non-null the steps are written into it as the decode
     * runs. {@code onlyParam} restricts the recording to one parameter; the
     * decode itself is unchanged either way, so a trace can never describe
     * different arithmetic from the value that reaches the dashboard.
     */
    public List<DecodedValue> decodeEntry(EntryDef entry, MessageDef msg, byte[] block,
                                          Quality blockQuality, Trace trace, String onlyParam) {
        List<DecodedValue> out = new ArrayList<>();
        if (msg == null) return out;

        byte[] raw = new byte[entry.tmRawLen()];
        boolean[] have = new boolean[entry.tmRawLen()];

        for (PickDef pk : entry.picks()) {
            for (int i = 0; i < pk.size(); i++) {
                int from = pk.block() + i;
                int to = pk.src() + i;
                if (from >= block.length || to >= raw.length) continue;
                raw[to] = block[from];
                have[to] = true;
            }
        }

        if (trace != null) {
            PickDef owner = null;
            for (PickDef pk : entry.picks()) {
                if (onlyParam == null || pk.paramNames().contains(onlyParam)) { owner = pk; break; }
            }
            trace.step("Layer 1d — the LUT says which bytes belong to this message")
                 .line("why", "nothing on the wire carries a name or an id",
                       "position inside the tier is the only link to the OBC")
                 .line("entry", entry.tcId() + "  (" + entry.subsystem() + " / " + entry.rsp() + ")")
                 .line("tier", entry.tier().name() + ", entry #" + entry.entryIndex())
                 .line("reply length", entry.tmRawLen() + " bytes on the wire")
                 .line("picks kept", entry.picks().size() + " run(s), "
                       + entry.picks().stream().mapToInt(PickDef::size).sum() + " bytes");
            if (owner != null) {
                trace.line("this parameter's pick",
                           "block[" + owner.block() + ":" + (owner.block() + owner.size())
                           + "]  ->  reply[" + owner.src() + ":" + (owner.src() + owner.size()) + "]");
                byte[] run = new byte[Math.min(owner.size(), Math.max(0, block.length - owner.block()))];
                System.arraycopy(block, owner.block(), run, 0, run.length);
                trace.hex("those bytes, out of the assembled block", run, owner.src(), null, null);
            }

            int sb0 = msg.bitAddressed() ? 0 : 0;   // highlight computed below, per parameter
            trace.step("Layer 1e — scatter back into the reply's own coordinates")
                 .line("kept", countTrue(have) + " of " + entry.tmRawLen() + " bytes")
                 .line("why", "putting each pick back at its src offset",
                       "makes the MIB's offsets apply unchanged; unkept bytes stay zero")
                 .hex("reconstructed reply", raw, sb0, null, null);
        }

        for (PickDef pk : entry.picks()) {
            for (String name : pk.paramNames()) {
                boolean want = trace != null && (onlyParam == null || onlyParam.equals(name));
                ParamDef p = msg.byName().get(name);
                if (p == null || p.bitOffset() == null || p.bitLength() == null) {
                    out.add(new DecodedValue(null, name, null, null, null,
                                             Quality.NOT_DOWNLINKED, "--"));
                    continue;
                }
                out.add(decodeOne(p, msg, raw, have, blockQuality, want ? trace : null));
            }
        }
        return out;
    }

    private static int countTrue(boolean[] b) {
        int n = 0;
        for (boolean x : b) if (x) n++;
        return n;
    }

    private DecodedValue decodeOne(ParamDef p, MessageDef msg, byte[] raw, boolean[] have,
                                   Quality blockQuality) {
        return decodeOne(p, msg, raw, have, blockQuality, null);
    }

    private DecodedValue decodeOne(ParamDef p, MessageDef msg, byte[] raw, boolean[] have,
                                   Quality blockQuality, Trace trace) {
        int lo = p.bitOffset();
        int hi = lo + p.bitLength();
        int b0 = lo / 8;
        int b1 = (hi + 7) / 8;

        if (trace != null) {
            trace.step("Layer 2 — the MIB says what those bytes mean")
                 .line("message", msg.name() + "  (" + msg.subsystem() + ", " + msg.direction() + ")")
                 .line("offset unit", msg.offsetUnit())
                 .line("offset", p.offset() + "   length " + p.length() + " " + msg.offsetUnit())
                 .line("as bits", "offset " + lo + ", width " + p.bitLength(),
                       p.byteAligned() ? "byte aligned" : "NOT byte aligned")
                 .line("byte range", "[" + b0 + ":" + b1 + "]")
                 .line("data type", p.dataType())
                 .line("endianness", p.bigEndian() ? "big" : "little")
                 .line("scale", p.scale() == null ? "none" : String.valueOf(p.scale()),
                       p.scale() == null ? null : "a multiplier: engineering = raw x scale")
                 .line("unit", p.unit() == null ? "—" : p.unit())
                 .hex("the reply, with this field highlighted", raw, 0, b0, b1);
        }

        if (b0 < 0 || b1 > raw.length) {
            if (trace != null) trace.step("Result")
                    .line("outcome", "NOT_DOWNLINKED", "the field lies outside the reply");
            return marker(p, Quality.NOT_DOWNLINKED);
        }
        for (int i = b0; i < b1; i++) {
            if (!have[i]) {
                Quality q = blockQuality == Quality.BLOCK_INCOMPLETE
                            ? Quality.BLOCK_INCOMPLETE : Quality.NOT_DOWNLINKED;
                if (trace != null) trace.step("Result")
                        .line("outcome", q.name(), "no pick claimed byte " + i
                              + " — it is zero fill, not data");
                return marker(p, q);
            }
        }
        boolean allFf = true;
        for (int i = b0; i < b1; i++) {
            if ((raw[i] & 0xFF) != 0xFF) { allFf = false; break; }
        }
        if (allFf) {
            if (trace != null) trace.step("Result")
                    .line("outcome", "NO_ANSWER",
                          "every byte is 0xFF — the subsystem did not reply, this is not 255");
            return marker(p, Quality.NO_ANSWER);
        }

        Object value = msg.bitAddressed()
                ? decodeBitField(p, getBits(raw, lo, p.bitLength()))
                : decodeByteField(p, raw, p.offset(), p.length());

        Quality q = blockQuality == Quality.OK ? Quality.OK : blockQuality;
        DecodedValue dv = toValue(p, value, q);

        if (trace != null) {
            byte[] field = new byte[b1 - b0];
            System.arraycopy(raw, b0, field, 0, field.length);
            trace.step("The arithmetic").hex("field bytes", field, b0, null, null);
            if (msg.bitAddressed()) {
                long rawBits = getBits(raw, lo, p.bitLength());
                trace.line("bit assembly", "LSB-first, bits " + lo + ".." + (hi - 1),
                           "bit b is in byte b>>3 at bit b&7 from the LSB; "
                           + "field bit i lands at result bit i")
                     .line("raw", rawBits + "  (0x" + Long.toHexString(rawBits).toUpperCase() + ")");
                if ("INT".equals(p.dataType())) {
                    trace.line("sign-extend", signExtend(rawBits, p.bitLength())
                               + "  over " + p.bitLength() + " bits");
                }
            } else {
                long u = getInt(raw, p.offset(), p.length(), p.bigEndian());
                trace.line((p.bigEndian() ? "big" : "little") + "-endian integer",
                           u + "  (0x" + Long.toHexString(u).toUpperCase() + ")");
                if ("INT".equals(p.dataType())) {
                    trace.line("sign-extend", signExtend(u, p.length() * 8)
                               + "  over " + (p.length() * 8) + " bits");
                }
                if ("FLOAT".equals(p.dataType()) && p.length() == 4) {
                    trace.line("IEEE-754 binary32",
                               Float.intBitsToFloat((int) u), "reinterpret the same 4 bytes");
                } else if ("DOUBLE".equals(p.dataType()) && p.length() == 8) {
                    trace.line("IEEE-754 binary64", Double.longBitsToDouble(u));
                }
            }
            if (p.scale() != null) {
                trace.line("apply scale", "raw x " + p.scale());
            }
            if (value instanceof EnumVal ev) {
                trace.line("enum lookup", ev.label() == null ? "no label for " + ev.value()
                                                             : ev.label(),
                           "scoped to this subsystem's enum table");
            }
            trace.step("Result")
                 .line(p.name(), dv.formatted())
                 .line("quality", dv.quality().name());
        }
        return dv;
    }

    private DecodedValue marker(ParamDef p, Quality q) {
        String text = switch (q) {
            case NO_ANSWER -> "-- (no answer)";
            case BLOCK_INCOMPLETE, NOT_DOWNLINKED -> "-- (block incomplete)";
            default -> "--";
        };
        return new DecodedValue(p.id(), p.name(), null, null, p.unit(), q, text);
    }

    // ── byte-addressed fields (EPS, GPS, Payload, SENSOR, UHF) ──────────────

    Object decodeByteField(ParamDef p, byte[] raw, int off, int len) {
        String t = p.dataType();
        boolean be = p.bigEndian();

        switch (t) {
            case "CHAR", "STRING" -> {
                int end = off;
                while (end < off + len && raw[end] != 0) end++;
                return new String(raw, off, end - off, StandardCharsets.US_ASCII);
            }
            case "HEX", "RESERVED" -> {
                // Rendered most-significant byte first, whichever order it arrived in.
                StringBuilder sb = new StringBuilder("0x");
                for (int k = 0; k < len; k++) {
                    int i = be ? k : len - 1 - k;
                    sb.append(String.format(Locale.ROOT, "%02X", raw[off + i] & 0xFF));
                }
                return sb.toString();
            }
            case "BOOL" -> {
                // Any non-zero byte in the field, not just the first.
                for (int i = 0; i < len; i++) if (raw[off + i] != 0) return Boolean.TRUE;
                return Boolean.FALSE;
            }
            case "ENUM" -> {
                long v = getInt(raw, off, len, be);
                return new EnumVal(v, label(p, v));
            }
            default -> { }
        }

        // A FLOAT that is not 4 bytes, or a DOUBLE that is not 8, falls through
        // to the integer path - exactly as the C++ codec does.
        if ("FLOAT".equals(t) && len == 4) {
            float f = Float.intBitsToFloat((int) getInt(raw, off, 4, be));
            return scaled(p, (double) f);
        }
        if ("DOUBLE".equals(t) && len == 8) {
            double d = Double.longBitsToDouble(getInt(raw, off, 8, be));
            return scaled(p, d);
        }

        long u = getInt(raw, off, len, be);
        if ("INT".equals(t)) {
            long v = signExtend(u, len * 8);
            return p.scale() != null ? (Object) (v * p.scale()) : (Object) v;
        }
        return p.scale() != null ? (Object) (u * p.scale()) : (Object) u;
    }

    // ── bit-addressed fields (ADCS, XLINK-S) ────────────────────────────────

    Object decodeBitField(ParamDef p, long raw) {
        String t = p.dataType();
        int bits = p.bitLength();

        switch (t) {
            case "BOOL" -> {
                return (raw & 1L) != 0;   // bit 0 only, whatever the declared width
            }
            case "ENUM" -> {
                return new EnumVal(raw, label(p, raw));
            }
            case "CHAR", "STRING" -> {
                int n = Math.max(bits / 8, 1);
                StringBuilder sb = new StringBuilder();
                for (int i = 0; i < n; i++) {
                    int b = (int) ((raw >>> (8 * i)) & 0xFF);
                    if (b == 0) break;
                    sb.append((char) b);
                }
                return sb.toString();
            }
            default -> { }
        }

        if ("FLOAT".equals(t) && bits == 32) {
            return scaled(p, (double) Float.intBitsToFloat((int) (raw & 0xFFFFFFFFL)));
        }
        if ("DOUBLE".equals(t) && bits == 64) {
            return scaled(p, Double.longBitsToDouble(raw));
        }
        if ("INT".equals(t)) {
            long v = signExtend(raw, bits);
            return p.scale() != null ? (Object) (v * p.scale()) : (Object) v;
        }
        return p.scale() != null ? (Object) (raw * p.scale()) : (Object) raw;
    }

    private Object scaled(ParamDef p, double d) {
        return p.scale() != null ? d * p.scale() : d;
    }

    private String label(ParamDef p, long v) {
        if (p.enumTypeId() == null) return null;
        Map<Long, String> m = enums.get(p.enumTypeId());
        return m == null ? null : m.get(v);
    }

    // ── primitives ──────────────────────────────────────────────────────────

    /** Unsigned read of {@code len} bytes. */
    static long getInt(byte[] raw, int off, int len, boolean bigEndian) {
        long v = 0;
        if (bigEndian) {
            for (int i = 0; i < len; i++) v = (v << 8) | (raw[off + i] & 0xFFL);
        } else {
            for (int i = len - 1; i >= 0; i--) v = (v << 8) | (raw[off + i] & 0xFFL);
        }
        return v;
    }

    /**
     * LSB-first bit assembly across the whole payload: absolute bit b lives in
     * byte b>>3 at bit b&7 counted from the least significant end, and field bit
     * i lands at result bit i. Fields wider than a byte therefore span byte
     * boundaries correctly.
     */
    static long getBits(byte[] raw, int bitOffset, int bits) {
        long v = 0;
        for (int i = 0; i < bits; i++) {
            int bi = bitOffset + i;
            int idx = bi >> 3;
            if (idx >= raw.length) break;
            if (((raw[idx] >> (bi & 7)) & 1) != 0) v |= 1L << i;
        }
        return v;
    }

    static long signExtend(long v, int bits) {
        if (bits >= 64) return v;
        long sign = 1L << (bits - 1);
        return (v & sign) != 0 ? v | ~((1L << bits) - 1) : v;
    }

    // ── rendering ───────────────────────────────────────────────────────────

    /** Mirrors the operator display in src/cli/value_format.cpp. */
    DecodedValue toValue(ParamDef p, Object value, Quality q) {
        String unit = p.unit();
        String suffix = (unit == null || unit.isEmpty()) ? "" : " " + unit;
        String t = p.dataType();

        if (value instanceof EnumVal ev) {
            String f = ev.value() + " (" + (ev.label() == null ? "?" : ev.label()) + ")";
            return new DecodedValue(p.id(), p.name(), (double) ev.value(), ev.label(), unit, q, f);
        }
        if (("FLOAT".equals(t) || "DOUBLE".equals(t)) && value instanceof Double d) {
            return new DecodedValue(p.id(), p.name(), d, null, unit, q, fixed(d, 3) + suffix);
        }
        if (value instanceof Double d) {
            // A non-float type that became fractional because a scale was applied.
            return new DecodedValue(p.id(), p.name(), d, null, unit, q, fixed(d, 2) + suffix);
        }
        if (value instanceof Boolean b) {
            return new DecodedValue(p.id(), p.name(), b ? 1.0 : 0.0, null, unit, q,
                                    b ? "true" : "false");
        }
        if (value instanceof Long l) {
            return new DecodedValue(p.id(), p.name(), (double) l, null, unit, q, l + suffix);
        }
        String s = String.valueOf(value);
        return new DecodedValue(p.id(), p.name(), null, s, unit, q, s + suffix);
    }

    /**
     * Fixed-point rendering of a double, matching the ground station's display.
     *
     * String.format("%.3f", d) is not equivalent: it pads the shortest
     * round-trippable representation with zeros, so a large magnitude prints as
     * ...460000000000000000 where the exact value of the double is
     * ...461596968490379509760. It also rounds half-away-from-zero, which
     * disagrees on ties. BigDecimal(double) is the exact binary value, and
     * HALF_EVEN is the tie rule the reference implementation uses.
     */
    static String fixed(double d, int scale) {
        if (Double.isNaN(d)) return "nan";
        if (Double.isInfinite(d)) return d > 0 ? "inf" : "-inf";
        String s = new BigDecimal(d).setScale(scale, RoundingMode.HALF_EVEN).toPlainString();
        // BigDecimal has no negative zero, and it drops the sign of a small
        // negative that rounds to zero. The display keeps both, so -1e-9 shows
        // as -0.000 rather than 0.000.
        boolean negative = d < 0.0 || (d == 0.0 && Double.doubleToRawLongBits(d) != 0L);
        if (negative && !s.startsWith("-")) {
            s = "-" + s;
        }
        return s;
    }

    /** A decoded enumeration: the raw value plus its label, if the table has one. */
    public record EnumVal(long value, String label) { }
}
