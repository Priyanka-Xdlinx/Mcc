package com.orblinx.mcc.wire;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Optional;

/**
 * A rolling window of recently received subframes, kept so that a past frame can
 * be selected and decoded again.
 *
 * Whole subframes are retained rather than the blocks assembled from them: a
 * trace should be able to show which subframes contributed to a block, and a
 * block alone cannot answer that. At 1280 bytes a subframe, the default window
 * is about 340 KB.
 */
public final class FrameBuffer {

    private final int capacity;
    private final Deque<Subframe> frames = new ArrayDeque<>();

    public FrameBuffer(int capacity) {
        this.capacity = capacity;
    }

    public synchronized void add(Subframe sf) {
        frames.addLast(sf);
        while (frames.size() > capacity) frames.removeFirst();
    }

    public synchronized int size() {
        return frames.size();
    }

    /** Newest first. */
    public synchronized List<Subframe> recent(int limit) {
        List<Subframe> out = new ArrayList<>();
        var it = frames.descendingIterator();
        while (it.hasNext() && out.size() < limit) out.add(it.next());
        return out;
    }

    public synchronized Optional<Subframe> get(long masterFrame, int subframe) {
        for (Subframe sf : frames) {
            if (sf.masterFrame() == masterFrame && sf.subframe() == subframe) {
                return Optional.of(sf);
            }
        }
        return Optional.empty();
    }

    public synchronized Optional<Subframe> latest() {
        return frames.isEmpty() ? Optional.empty() : Optional.of(frames.peekLast());
    }

    /**
     * The subframes that make up the tier block containing the given subframe,
     * in slice order. Missing slices come back as null - a block with a hole must
     * be visibly incomplete rather than quietly short.
     */
    public synchronized Subframe[] blockSlices(Tier tier, long masterFrame, int subframe) {
        long blockIndex = (subframe - 1) / tier.blockSpan;
        Subframe[] slices = new Subframe[tier.blockSpan];
        for (Subframe sf : frames) {
            if (sf.masterFrame() != masterFrame) continue;
            if ((sf.subframe() - 1) / tier.blockSpan != blockIndex) continue;
            slices[tier.sliceIndex(sf.subframe())] = sf;
        }
        return slices;
    }

    /** Concatenate the slices into a block, or empty if any slice is missing. */
    public static Optional<byte[]> assemble(Tier tier, Subframe[] slices) {
        for (Subframe s : slices) if (s == null) return Optional.empty();
        byte[] block = new byte[tier.blockSpan * tier.segBytes];
        for (int i = 0; i < slices.length; i++) {
            System.arraycopy(tier.segmentOf(slices[i].payload()), 0,
                             block, i * tier.segBytes, tier.segBytes);
        }
        return Optional.of(block);
    }
}
