package com.orblinx.mcc.decode;

import com.orblinx.mcc.store.SchemaRepo;
import com.orblinx.mcc.wire.FrameBuffer;
import com.orblinx.mcc.wire.Subframe;
import com.orblinx.mcc.wire.Tier;
import com.orblinx.mcc.wire.XmccHeader;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import org.springframework.stereotype.Service;

/**
 * Builds the end-to-end story for one parameter on one subframe: the bytes that
 * arrived, how Layer 1 decided which message they belong to, and how Layer 2
 * turned them into a number.
 *
 * Steps 1a-1c are assembled here because they are properties of the frame rather
 * than of the parameter. Everything from the LUT lookup onward is recorded by
 * {@link ParameterDecoder} on its ordinary decode path, so the trace cannot drift
 * away from the value the dashboard shows.
 */
@Service
public class TraceService {

    private final SchemaRepo schema;
    private final ParameterDecoder decoder;

    public TraceService(SchemaRepo schema) {
        this.schema = schema;
        this.decoder = new ParameterDecoder(schema.enums());
    }

    public Map<String, Object> trace(FrameBuffer buffer, int parameterId,
                                     Long masterFrame, Integer subframe) {
        Map<String, Object> out = new LinkedHashMap<>();

        EntryDef entry = schema.entryForParameter(parameterId);
        if (entry == null) {
            out.put("error", "parameter " + parameterId
                    + " is not downlinked by the current LUT");
            return out;
        }
        String paramName = schema.parameterName(parameterId);
        MessageDef msg = schema.message(entry.messageId());
        Tier tier = entry.tier();

        Optional<Subframe> picked = (masterFrame != null && subframe != null)
                ? buffer.get(masterFrame, subframe)
                : buffer.latest();
        if (picked.isEmpty()) {
            out.put("error", masterFrame == null
                    ? "no frames received yet — start the simulator"
                    : "frame MF#" + masterFrame + " SF" + subframe + " is no longer buffered");
            return out;
        }
        Subframe sf = picked.get();

        Trace t = new Trace();

        // ── 1a. the header ──────────────────────────────────────────────────
        byte[] h = sf.header();
        t.step("Layer 1a — the 26-byte XMCC header")
         .line("magic", new String(h, 0, 4), "bytes 0-3; anything else means we are out of sync")
         .line("version", h[4] & 0xFF, "byte 4; a higher version may have a longer header")
         .line("master frame", sf.masterFrame(), "bytes 6-9, unsigned 32-bit")
         .line("subframe", sf.subframe() + " of 16", "bytes 10-11, 1-based")
         .line("spacecraft time", sf.spacecraftTime(),
               "bytes 12-20; copied from the TM header, not a ground receive time")
         .line("payload length", sf.payload().length,
               "bytes 22-25; read it rather than assuming 1280")
         .hex("header as received", h, 0, null, null);

        // ── 1b. the tier split ──────────────────────────────────────────────
        t.step("Layer 1b — split the payload into four tier segments");
        for (Tier x : Tier.values()) {
            t.line(x.name(),
                   "payload[" + x.segOffset + ":" + (x.segOffset + x.segBytes) + "]  "
                   + x.segBytes + " bytes",
                   x == tier ? "this parameter lives here · block spans " + x.blockSpan
                               + " subframe(s)"
                             : "block spans " + x.blockSpan + " subframe(s)");
        }
        t.line("reserved", "payload[1275:1280]", "sent as zero");
        byte[] seg = tier.segmentOf(sf.payload());
        t.hex(tier.name() + " segment carried by this subframe", seg, tier.segOffset, null, null);

        // ── 1c. block assembly ──────────────────────────────────────────────
        Subframe[] slices = buffer.blockSlices(tier, sf.masterFrame(), sf.subframe());
        t.step("Layer 1c — assemble the " + tier.name() + " block")
         .line("rule", "slice index = (subframe - 1) % " + tier.blockSpan)
         .line("block size", tier.blockSpan + " x " + tier.segBytes + " = "
               + (tier.blockSpan * tier.segBytes) + " bytes");
        List<Map<String, Object>> sliceInfo = new ArrayList<>();
        for (int i = 0; i < slices.length; i++) {
            Map<String, Object> s = new LinkedHashMap<>();
            s.put("index", i);
            s.put("present", slices[i] != null);
            s.put("subframe", slices[i] == null ? null : slices[i].subframe());
            sliceInfo.add(s);
            t.line("slice " + i,
                   slices[i] == null ? "missing" : "from subframe " + slices[i].subframe(),
                   slices[i] == null ? "lost on the downlink — the block is stale" : null);
        }

        Optional<byte[]> assembled = FrameBuffer.assemble(tier, slices);
        if (assembled.isEmpty()) {
            t.step("Result").line("outcome", "BLOCK_INCOMPLETE",
                    "a slice of this block never arrived, so its values cannot be trusted");
            out.put("steps", t.steps());
            fillHeader(out, entry, paramName, msg, sf, sliceInfo, false);
            return out;
        }

        // ── 1d onward: recorded by the decoder itself ───────────────────────
        List<DecodedValue> values =
                decoder.decodeEntry(entry, msg, assembled.get(), Quality.OK, t, paramName);

        out.put("steps", t.steps());
        fillHeader(out, entry, paramName, msg, sf, sliceInfo, true);
        values.stream()
              .filter(v -> paramName.equals(v.name()))
              .findFirst()
              .ifPresent(v -> {
                  out.put("value", v.formatted());
                  out.put("quality", v.quality().name());
              });
        return out;
    }

    private void fillHeader(Map<String, Object> out, EntryDef entry, String paramName,
                            MessageDef msg, Subframe sf, List<Map<String, Object>> slices,
                            boolean complete) {
        out.put("parameter", paramName);
        out.put("entry", entry.tcId());
        out.put("subsystem", entry.subsystem());
        out.put("message", msg == null ? entry.rsp() : msg.name());
        out.put("tier", entry.tier().name());
        out.put("blockSpan", entry.tier().blockSpan);
        out.put("masterFrame", sf.masterFrame());
        out.put("subframe", sf.subframe());
        out.put("spacecraftTime", sf.spacecraftTime());
        out.put("slices", slices);
        out.put("blockComplete", complete);
        out.put("headerLength", XmccHeader.LENGTH);
    }
}
