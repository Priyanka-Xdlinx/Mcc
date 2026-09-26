package com.orblinx.mcc.decode;

import com.orblinx.mcc.store.SchemaRepo;
import com.orblinx.mcc.wire.FrameBuffer;
import com.orblinx.mcc.wire.Subframe;
import com.orblinx.mcc.wire.Tier;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;

import org.springframework.stereotype.Service;

/**
 * Decodes an entire subframe: every tier, every lookup-table entry, every
 * parameter, plus a byte-by-byte account of who owns each of the 1280 payload
 * bytes.
 *
 * The per-parameter trace answers "how did this number happen". This answers the
 * other question - "what is actually in a frame" - which is the one that makes
 * the format legible rather than a sequence of offsets to be taken on faith.
 */
@Service
public class FrameDecodeService {

    /** What a payload byte is doing. Ordinal order is the drawing order. */
    public enum ByteRole { RESERVED, UNCLAIMED, PICKED }

    private final SchemaRepo schema;
    private final ParameterDecoder decoder;

    public FrameDecodeService(SchemaRepo schema) {
        this.schema = schema;
        this.decoder = new ParameterDecoder(schema.enums());
    }

    public Map<String, Object> decodeFrame(FrameBuffer buffer, Long mf, Integer sf) {
        Map<String, Object> out = new LinkedHashMap<>();

        Optional<Subframe> found = (mf != null && sf != null)
                ? buffer.get(mf, sf) : buffer.latest();
        if (found.isEmpty()) {
            out.put("error", mf == null
                    ? "no frames received yet — start the simulator"
                    : "MF#" + mf + " SF" + sf + " is no longer buffered");
            return out;
        }
        Subframe frame = found.get();
        byte[] payload = frame.payload();

        out.put("masterFrame", frame.masterFrame());
        out.put("subframe", frame.subframe());
        out.put("spacecraftTime", frame.spacecraftTime());
        out.put("payloadLength", payload.length);
        out.put("header", hex(frame.header()));

        // Owner of every payload byte, for the map. Default: unclaimed.
        int[] owner = new int[payload.length];          // index into `legend`, -1 = none
        java.util.Arrays.fill(owner, -1);
        List<Map<String, Object>> legend = new ArrayList<>();

        List<Map<String, Object>> tiers = new ArrayList<>();
        int totalParams = 0, totalDecoded = 0;

        for (Tier tier : Tier.values()) {
            Map<String, Object> t = new LinkedHashMap<>();
            t.put("tier", tier.name());
            t.put("segOffset", tier.segOffset);
            t.put("segBytes", tier.segBytes);
            t.put("blockSpan", tier.blockSpan);
            int sliceIndex = tier.sliceIndex(frame.subframe());
            t.put("sliceIndex", sliceIndex);
            t.put("completesHere", sliceIndex == tier.blockSpan - 1);

            Subframe[] slices = buffer.blockSlices(tier, frame.masterFrame(), frame.subframe());
            Optional<byte[]> block = FrameBuffer.assemble(tier, slices);
            t.put("blockComplete", block.isPresent());
            List<Integer> present = new ArrayList<>();
            for (int i = 0; i < slices.length; i++) if (slices[i] != null) present.add(i);
            t.put("slicesPresent", present);

            List<Map<String, Object>> entries = new ArrayList<>();
            for (EntryDef e : schema.entriesOf(tier)) {
                MessageDef msg = schema.message(e.messageId());
                Map<String, Object> em = new LinkedHashMap<>();
                em.put("entry", e.tcId());
                em.put("subsystem", e.subsystem());
                em.put("message", msg == null ? e.rsp() : msg.name());
                em.put("tmRawLen", e.tmRawLen());
                em.put("bitAddressed", msg != null && msg.bitAddressed());

                // Which bytes of THIS subframe belong to this entry.
                List<Map<String, Object>> picks = new ArrayList<>();
                int legendIndex = legend.size();
                for (PickDef pk : e.picks()) {
                    Map<String, Object> pm = new LinkedHashMap<>();
                    pm.put("src", pk.src());
                    pm.put("size", pk.size());
                    pm.put("block", pk.block());
                    // A pick lives in the assembled block; only the slice this
                    // subframe carried is visible here.
                    int sliceStart = sliceIndex * tier.segBytes;
                    int from = Math.max(pk.block(), sliceStart);
                    int to = Math.min(pk.block() + pk.size(), sliceStart + tier.segBytes);
                    if (to > from) {
                        int payFrom = tier.segOffset + (from - sliceStart);
                        int payTo = tier.segOffset + (to - sliceStart);
                        pm.put("payloadFrom", payFrom);
                        pm.put("payloadTo", payTo);
                        for (int i = payFrom; i < payTo && i < owner.length; i++) {
                            owner[i] = legendIndex;
                        }
                    } else {
                        pm.put("payloadFrom", null);
                        pm.put("payloadTo", null);
                    }
                    picks.add(pm);
                }
                legend.add(Map.of("entry", e.tcId(), "subsystem", e.subsystem(),
                                  "tier", tier.name()));

                List<Map<String, Object>> params = new ArrayList<>();
                List<DecodedValue> values = block.isPresent()
                        ? decoder.decodeEntry(e, msg, block.get(), Quality.OK)
                        : List.of();
                Map<String, DecodedValue> byName = new LinkedHashMap<>();
                for (DecodedValue v : values) byName.put(v.name(), v);

                // Rebuild the reply exactly as the decoder does, so the bytes
                // shown beside each value are the bytes it actually read.
                byte[] reply = new byte[e.tmRawLen()];
                boolean[] have = new boolean[e.tmRawLen()];
                if (block.isPresent()) {
                    byte[] blk = block.get();
                    for (PickDef pk : e.picks()) {
                        for (int i = 0; i < pk.size(); i++) {
                            int from = pk.block() + i, to = pk.src() + i;
                            if (from >= blk.length || to >= reply.length) continue;
                            reply[to] = blk[from];
                            have[to] = true;
                        }
                    }
                }
                em.put("replyHex", hex(reply));
                List<Integer> haveMask = new ArrayList<>(reply.length);
                for (boolean b : have) haveMask.add(b ? 1 : 0);
                em.put("replyHave", haveMask);

                // Listed in the message's own `seq` order, the way the parameter
                // dictionary and the decode contract list them - not in pick
                // order, which is a property of the lookup table rather than of
                // the message and reads as arbitrary.
                List<String> names = new ArrayList<>();
                for (PickDef pk : e.picks()) names.addAll(pk.paramNames());
                names.sort((a, b) -> {
                    ParamDef pa = msg == null ? null : msg.byName().get(a);
                    ParamDef pb = msg == null ? null : msg.byName().get(b);
                    int sa = pa == null ? Integer.MAX_VALUE : pa.seq();
                    int sb = pb == null ? Integer.MAX_VALUE : pb.seq();
                    return sa != sb ? Integer.compare(sa, sb) : a.compareTo(b);
                });

                for (String name : names) {
                    ParamDef p = msg == null ? null : msg.byName().get(name);
                    DecodedValue v = byName.get(name);
                    Map<String, Object> pm = new LinkedHashMap<>();
                    pm.put("seq", p == null ? null : p.seq());
                    pm.put("uid", p == null || msg == null ? null : msg.parameterUid(p.seq()));
                    pm.put("name", name);
                    pm.put("subsystem", e.subsystem());
                    pm.put("parameterId", p == null ? null : p.id());
                    if (p != null && p.bitOffset() != null && p.bitLength() != null) {
                        int b0 = p.bitOffset() / 8;
                        int b1 = (p.bitOffset() + p.bitLength() + 7) / 8;
                        pm.put("byteFrom", b0);
                        pm.put("byteTo", b1);
                        pm.put("bitOffset", p.bitOffset());
                        pm.put("bitLength", p.bitLength());
                        pm.put("byteAligned", p.byteAligned());
                        pm.put("dataType", p.dataType());
                        pm.put("endian", p.bigEndian() ? "big" : "little");
                        pm.put("unit", p.unit());
                        pm.put("scale", p.scale());

                        // The bytes this value was actually read from, and - for a
                        // bit field - the bits themselves, LSB-first as the codec
                        // assembles them. A sub-byte field is otherwise impossible
                        // to check by eye against a byte-level hex dump.
                        if (b1 <= reply.length && b0 >= 0) {
                            StringBuilder h = new StringBuilder();
                            boolean complete = true;
                            for (int i = b0; i < b1; i++) {
                                if (h.length() > 0) h.append(' ');
                                h.append(String.format("%02x", reply[i] & 0xFF));
                                if (!have[i]) complete = false;
                            }
                            pm.put("rawHex", h.toString());
                            pm.put("rawComplete", complete);

                            StringBuilder bits = new StringBuilder();
                            for (int i = p.bitLength() - 1; i >= 0; i--) {
                                int bi = p.bitOffset() + i;
                                bits.append((reply[bi >> 3] >> (bi & 7)) & 1);
                            }
                            pm.put("rawBits", bits.toString());
                        }
                    }
                    pm.put("value", v == null ? null : v.formatted());
                    pm.put("quality", v == null ? null : v.quality().name());
                    params.add(pm);
                    totalParams++;
                    if (v != null && v.quality() == Quality.OK) totalDecoded++;
                }
                em.put("picks", picks);
                em.put("parameters", params);
                entries.add(em);
            }
            t.put("entries", entries);
            t.put("hex", hex(tier.segmentOf(payload)));
            tiers.add(t);
        }

        // The five trailing bytes are reserved and sent as zero.
        for (int i = 1275; i < payload.length; i++) owner[i] = -2;

        out.put("tiers", tiers);
        out.put("legend", legend);
        out.put("byteOwner", owner);
        out.put("payloadHex", hex(payload));
        out.put("totalParameters", totalParams);
        out.put("decodedOk", totalDecoded);
        return out;
    }

    private static List<String> hex(byte[] b) {
        List<String> out = new ArrayList<>(b.length);
        for (byte x : b) out.add(String.format("%02x", x & 0xFF));
        return out;
    }
}
