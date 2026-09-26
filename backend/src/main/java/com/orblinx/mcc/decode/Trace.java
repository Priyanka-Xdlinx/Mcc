package com.orblinx.mcc.decode;

import java.util.ArrayList;
import java.util.List;

/**
 * A record of what the decoder actually did, step by step, for one parameter.
 *
 * This is filled in by {@link ParameterDecoder} on its normal code path - the
 * decoder does not have a separate "explain" implementation. A trace produced by
 * a parallel copy of the arithmetic could disagree with the value the dashboard
 * shows and nobody would notice, which would make it worse than no trace at all.
 */
public final class Trace {

    /** One labelled fact inside a step. */
    public record Line(String label, String value, String note) {}

    /** A hex dump, optionally highlighting the bytes the current step is about. */
    public record Hex(String title, int baseOffset, List<String> bytes,
                      Integer markFrom, Integer markTo) {}

    public record Step(String title, List<Line> lines, List<Hex> hex) {}

    private final List<Step> steps = new ArrayList<>();
    private List<Line> lines = new ArrayList<>();
    private List<Hex> hex = new ArrayList<>();
    private String title;

    public Trace step(String title) {
        flush();
        this.title = title;
        return this;
    }

    public Trace line(String label, Object value) {
        return line(label, value, null);
    }

    public Trace line(String label, Object value, String note) {
        lines.add(new Line(label, String.valueOf(value), note));
        return this;
    }

    public Trace hex(String title, byte[] data, int baseOffset, Integer from, Integer to) {
        List<String> out = new ArrayList<>(data.length);
        for (byte b : data) out.add(String.format("%02x", b & 0xFF));
        hex.add(new Hex(title, baseOffset, out, from, to));
        return this;
    }

    private void flush() {
        if (title != null) {
            steps.add(new Step(title, lines, hex));
            lines = new ArrayList<>();
            hex = new ArrayList<>();
        }
    }

    public List<Step> steps() {
        flush();
        title = null;
        return steps;
    }
}
