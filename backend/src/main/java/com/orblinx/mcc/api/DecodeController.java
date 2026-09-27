package com.orblinx.mcc.api;

import com.orblinx.mcc.IngestService;
import com.orblinx.mcc.decode.TraceService;
import com.orblinx.mcc.wire.Subframe;
import com.orblinx.mcc.wire.Tier;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.springframework.web.bind.annotation.*;

/**
 * The decode-inspection surface: which frames are still buffered, what a given
 * frame contains, and the full Layer 1 / Layer 2 story for one parameter on one
 * frame.
 */
@RestController
@RequestMapping("/api")
@Tag(name = "Decode")
public class DecodeController {

    private final IngestService ingest;
    private final TraceService tracer;
    private final com.orblinx.mcc.decode.FrameDecodeService frameDecoder;

    public DecodeController(IngestService ingest, TraceService tracer,
                            com.orblinx.mcc.decode.FrameDecodeService frameDecoder) {
        this.ingest = ingest;
        this.tracer = tracer;
        this.frameDecoder = frameDecoder;
    }

    /**
     * The whole subframe decoded: every tier, every entry, every parameter, plus
     * an owner for each of the 1280 payload bytes.
     */
    @GetMapping("/frame-decode")
    @Operation(summary = "One subframe, fully decoded",
               description = "Every tier, LUT entry and parameter in the subframe, and the owner of each "
                           + "of the 1,280 payload bytes. Without mf/sf it uses the newest subframe.")
    public Map<String, Object> frameDecode(
            @Parameter(description = "Master frame number") @RequestParam(required = false) Long mf,
            @Parameter(description = "Subframe number, 1 to 16") @RequestParam(required = false) Integer sf) {
        return frameDecoder.decodeFrame(ingest.frames(), mf, sf);
    }

    /** Recent subframes, newest first, for the frame picker. */
    @GetMapping("/frames")
    @Operation(summary = "Buffered subframes, newest first",
               description = "The last subframes held in memory (up to 320 = 20 master frames), with the "
                           + "tiers whose block completes in each.")
    public List<Map<String, Object>> frames(
            @Parameter(description = "Maximum subframes, 1 to 320") @RequestParam(defaultValue = "320") int limit) {
        List<Map<String, Object>> out = new ArrayList<>();
        for (Subframe sf : ingest.frames().recent(Math.min(Math.max(limit, 1), 320))) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("masterFrame", sf.masterFrame());
            m.put("subframe", sf.subframe());
            m.put("spacecraftTime", sf.spacecraftTime());
            m.put("payloadLength", sf.payload().length);
            // Which tier blocks finish on this subframe - the moment those
            // parameters become decodable.
            List<String> completes = new ArrayList<>();
            for (Tier t : Tier.values()) {
                if (t.sliceIndex(sf.subframe()) == t.blockSpan - 1) completes.add(t.name());
            }
            m.put("completes", completes);
            out.add(m);
        }
        return out;
    }

    /** One frame in full: the header, and each tier segment as hex. */
    @GetMapping("/frames/{masterFrame}/{subframe}")
    @Operation(summary = "One buffered subframe as received",
               description = "The 26-byte XMCC header and each tier segment of the payload as hex.")
    public Map<String, Object> frame(@Parameter(description = "Master frame number") @PathVariable long masterFrame,
                                     @Parameter(description = "Subframe number, 1 to 16") @PathVariable int subframe) {
        Map<String, Object> out = new LinkedHashMap<>();
        var found = ingest.frames().get(masterFrame, subframe);
        if (found.isEmpty()) {
            out.put("error", "MF#" + masterFrame + " SF" + subframe + " is no longer buffered");
            return out;
        }
        Subframe sf = found.get();
        out.put("masterFrame", sf.masterFrame());
        out.put("subframe", sf.subframe());
        out.put("spacecraftTime", sf.spacecraftTime());
        out.put("header", hex(sf.header()));
        out.put("payloadLength", sf.payload().length);

        List<Map<String, Object>> tiers = new ArrayList<>();
        for (Tier t : Tier.values()) {
            Map<String, Object> m = new LinkedHashMap<>();
            m.put("tier", t.name());
            m.put("offset", t.segOffset);
            m.put("bytes", t.segBytes);
            m.put("blockSpan", t.blockSpan);
            m.put("sliceIndex", t.sliceIndex(sf.subframe()));
            m.put("completesHere", t.sliceIndex(sf.subframe()) == t.blockSpan - 1);
            m.put("hex", hex(t.segmentOf(sf.payload())));
            tiers.add(m);
        }
        out.put("tiers", tiers);
        return out;
    }

    /**
     * The full trace for one parameter. Without mf/sf it uses the newest frame,
     * which is what the live view follows.
     */
    @GetMapping("/trace")
    @Operation(summary = "Decode trace for one parameter",
               description = "Every step from payload bytes to engineering value: XMCC header, tier split, "
                           + "block assembly, LUT picks, MIB field (offset, type, endianness, scale) and "
                           + "the arithmetic. Produced by the decoder on its normal path.")
    public Map<String, Object> trace(
            @Parameter(description = "Parameter id", example = "2168") @RequestParam int parameterId,
            @Parameter(description = "Master frame number; newest if omitted") @RequestParam(required = false) Long mf,
            @Parameter(description = "Subframe number, 1 to 16") @RequestParam(required = false) Integer sf) {
        return tracer.trace(ingest.frames(), parameterId, mf, sf);
    }

    private static List<String> hex(byte[] b) {
        List<String> out = new ArrayList<>(b.length);
        for (byte x : b) out.add(String.format("%02x", x & 0xFF));
        return out;
    }
}
