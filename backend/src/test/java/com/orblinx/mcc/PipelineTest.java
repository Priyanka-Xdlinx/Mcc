package com.orblinx.mcc;

import com.orblinx.mcc.decode.*;
import com.orblinx.mcc.wire.*;

import java.util.*;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;

import static org.junit.jupiter.api.Assertions.*;

/** Level 2 - the wire layer, independent of any captured data. */
class PipelineTest {

    @Test
    @DisplayName("C2 a tier block completes only when every slice has arrived")
    void c2_tierAssembly() {
        TierAssembler a = new TierAssembler();
        assertEquals(1, Tier.VH.blockSpan);
        assertEquals(2, Tier.H.blockSpan);
        assertEquals(4, Tier.M.blockSpan);
        assertEquals(8, Tier.L.blockSpan);

        // VH is complete in a single subframe; H needs two.
        assertNotNull(a.feed(Tier.VH, sub(1, 1)));
        assertNull(a.feed(Tier.H, sub(1, 1)), "H is only half filled after one subframe");
        byte[] h = a.feed(Tier.H, sub(1, 2));
        assertNotNull(h, "H completes on the second subframe");
        assertEquals(Tier.H.blockSpan * Tier.H.segBytes, h.length);

        for (int sf = 1; sf <= 3; sf++) assertNull(a.feed(Tier.M, sub(1, sf)));
        assertNotNull(a.feed(Tier.M, sub(1, 4)), "M completes on the fourth");
    }

    @Test
    @DisplayName("C2 slice index is (subframe - 1) % span, so slices land in order")
    void c2_sliceIndex() {
        assertEquals(0, Tier.H.sliceIndex(1));
        assertEquals(1, Tier.H.sliceIndex(2));
        assertEquals(0, Tier.H.sliceIndex(3));
        assertEquals(3, Tier.M.sliceIndex(4));
        assertEquals(7, Tier.L.sliceIndex(8));
        assertEquals(0, Tier.L.sliceIndex(9));
    }

    @Test
    @DisplayName("C3 picks are read at their absolute block offset, not sequentially")
    void c3_absoluteBlockOffsets() {
        // Two picks whose block offsets are deliberately out of order and not
        // adjacent. Walking the block sequentially would read the wrong bytes
        // while still producing numbers.
        byte[] block = new byte[Tier.VH.blockBudget];
        block[200] = 0x11;      // first pick's byte lives late in the block
        block[10]  = 0x22;      // second pick's byte lives early

        ParamDef pa = param(1, "alpha", 0);
        ParamDef pb = param(2, "beta", 1);
        MessageDef msg = new MessageDef(1, "TEST", "RSP", "TM", "bytes", 2, "NODE", 7,
                List.of(pa, pb), Map.of("alpha", pa, "beta", pb));
        EntryDef entry = new EntryDef(1, Tier.VH, 0, "T-001", "TEST", "RSP", 99, 2, 1,
                List.of(new PickDef(1, 0, 0, 1, 200, List.of("alpha")),
                        new PickDef(2, 1, 1, 1, 10,  List.of("beta"))));

        List<DecodedValue> out = new ParameterDecoder(Map.of())
                .decodeEntry(entry, msg, block, Quality.OK);

        assertEquals(2, out.size());
        assertEquals("17", out.get(0).formatted(), "alpha must come from block[200]");
        assertEquals("34", out.get(1).formatted(), "beta must come from block[10]");
    }

    @Test
    @DisplayName("C5 a block missing a slice is reported, never decoded as data")
    void c5_gapHandling() {
        TierAssembler a = new TierAssembler();
        a.feed(Tier.M, sub(1, 1));
        a.feed(Tier.M, sub(1, 2));
        // subframe 3 lost on the downlink
        a.feed(Tier.M, sub(1, 4));
        assertEquals(3, a.sliceCount(Tier.M, sub(1, 4)));

        List<TierAssembler.Pending> abandoned = a.drainBefore(2);
        assertEquals(1, abandoned.size(), "the incomplete M block must be reported");
        assertEquals(Tier.M, abandoned.get(0).tier());
        assertEquals(3, abandoned.get(0).slicesHave());
        assertEquals(4, abandoned.get(0).tier().blockSpan);
        assertTrue(a.drainBefore(2).isEmpty(), "reporting it also clears it");
    }

    @Test
    @DisplayName("C6 the header is rejected rather than misparsed")
    void c6_framing() {
        byte[] h = header(1, 7, 1280);
        XmccHeader ok = XmccHeader.parse(h);
        assertEquals(1, ok.masterFrame);
        assertEquals(7, ok.subframe);
        assertEquals(1280, ok.payloadLength, "length comes from the field, not a constant");

        byte[] shortPayload = header(1, 7, 640);
        assertEquals(640, XmccHeader.parse(shortPayload).payloadLength);

        byte[] badMagic = header(1, 7, 1280);
        badMagic[1] = 'X';
        assertThrows(XmccHeader.OutOfSyncException.class, () -> XmccHeader.parse(badMagic));

        byte[] futureVersion = header(1, 7, 1280);
        futureVersion[4] = 2;
        // A newer header may have grown; guessing at it would silently shift every field.
        assertThrows(IllegalStateException.class, () -> XmccHeader.parse(futureVersion));
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    private static Subframe sub(long mf, int sf) {
        return new Subframe(mf, sf, "2026-09-21 11:05:26.120",
                            new byte[XmccHeader.LENGTH], new byte[1280]);
    }

    private static ParamDef param(int id, String name, int offset) {
        return new ParamDef(id, 1, id, name, offset, 1, offset * 8, 8, true,
                            "UINT", false, null, null, null, false);
    }

    private static byte[] header(long mf, int sf, int len) {
        byte[] h = new byte[XmccHeader.LENGTH];
        h[0] = 'X'; h[1] = 'M'; h[2] = 'C'; h[3] = 'C';
        h[4] = 1;
        h[6]  = (byte) (mf >> 24); h[7] = (byte) (mf >> 16);
        h[8]  = (byte) (mf >> 8);  h[9] = (byte) mf;
        h[10] = (byte) (sf >> 8);  h[11] = (byte) sf;
        h[12] = 0x07; h[13] = (byte) 0xEA;      // year 2026
        h[14] = 9; h[15] = 21; h[16] = 11; h[17] = 5; h[18] = 26;
        h[22] = (byte) (len >> 24); h[23] = (byte) (len >> 16);
        h[24] = (byte) (len >> 8);  h[25] = (byte) len;
        return h;
    }
}
