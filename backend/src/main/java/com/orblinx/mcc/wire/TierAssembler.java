package com.orblinx.mcc.wire;

import java.util.HashMap;
import java.util.Map;

/**
 * Collects the slices of each tier block until the block is complete.
 *
 * A block is only valid once EVERY slice has arrived. If a subframe was lost on
 * the downlink, the block containing it is incomplete - those values are stale,
 * not data - so the partial block is reported rather than silently decoded.
 */
public final class TierAssembler {

    private record Key(Tier tier, long masterFrame, long blockIndex) {}

    private final Map<Key, byte[][]> slices = new HashMap<>();

    /** Feed one subframe; returns the assembled block, or null while still filling. */
    public byte[] feed(Tier tier, Subframe sf) {
        Key key = new Key(tier, sf.masterFrame(), sf.blockIndex(tier));
        byte[][] have = slices.computeIfAbsent(key, k -> new byte[tier.blockSpan][]);
        have[tier.sliceIndex(sf.subframe())] = tier.segmentOf(sf.payload());

        for (byte[] s : have) {
            if (s == null) return null;
        }
        slices.remove(key);
        byte[] block = new byte[tier.blockSpan * tier.segBytes];
        for (int i = 0; i < have.length; i++) {
            System.arraycopy(have[i], 0, block, i * tier.segBytes, tier.segBytes);
        }
        return block;
    }

    /** How many slices of this block have arrived so far. */
    public int sliceCount(Tier tier, Subframe sf) {
        byte[][] have = slices.get(new Key(tier, sf.masterFrame(), sf.blockIndex(tier)));
        if (have == null) return 0;
        int n = 0;
        for (byte[] s : have) if (s != null) n++;
        return n;
    }

    /** A block that never received all of its slices. */
    public record Pending(Tier tier, long masterFrame, long blockIndex, int slicesHave) {}

    /**
     * Remove partial blocks older than the given master frame and report them.
     *
     * A block abandoned this way had a subframe lost on the downlink. Its
     * parameters must be reported as incomplete rather than decoded from a
     * half-filled buffer, which would look like ordinary data.
     */
    public java.util.List<Pending> drainBefore(long masterFrame) {
        java.util.List<Pending> out = new java.util.ArrayList<>();
        var it = slices.entrySet().iterator();
        while (it.hasNext()) {
            var e = it.next();
            if (e.getKey().masterFrame() >= masterFrame) continue;
            int n = 0;
            for (byte[] s : e.getValue()) if (s != null) n++;
            out.add(new Pending(e.getKey().tier(), e.getKey().masterFrame(),
                                e.getKey().blockIndex(), n));
            it.remove();
        }
        return out;
    }
}
