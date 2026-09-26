package com.orblinx.mcc.wire;

/**
 * The four priority tiers inside a 1280-byte subframe payload.
 *
 * Only VH is complete in a single subframe. H, M and L are slices of a larger
 * block that must be concatenated across consecutive subframes before their
 * contents mean anything - a block holding a missing slice is stale, not data.
 */
public enum Tier {
    VH(0,   400, 1,  400),
    H (400, 400, 2,  800),
    M (800, 300, 4, 1200),
    L (1100, 175, 8, 1400);

    public final int segOffset;
    public final int segBytes;
    public final int blockSpan;     // subframes needed to complete the block
    public final int blockBudget;

    Tier(int segOffset, int segBytes, int blockSpan, int blockBudget) {
        this.segOffset = segOffset;
        this.segBytes = segBytes;
        this.blockSpan = blockSpan;
        this.blockBudget = blockBudget;
    }

    /** Which slice of this tier's block the given 1-based subframe carries. */
    public int sliceIndex(int subframe) {
        return (subframe - 1) % blockSpan;
    }

    public byte[] segmentOf(byte[] payload) {
        byte[] out = new byte[segBytes];
        System.arraycopy(payload, segOffset, out, 0, segBytes);
        return out;
    }
}
