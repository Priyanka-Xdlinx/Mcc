package com.orblinx.mcc.wire;

/**
 * One XMCC message: a 26-byte header plus the subframe payload.
 *
 * {@code spacecraftTime} is the CCS Time Code copied field-for-field out of the
 * TM header. It is NOT a ground receive time and NOT a Unix epoch - passing it
 * through verbatim keeps it free of timezone and leap-second handling, so we
 * see exactly what the spacecraft stamped.
 */
public record Subframe(long masterFrame, int subframe, String spacecraftTime,
                       byte[] header, byte[] payload) {

    /** Which block of this tier the subframe belongs to, within its master frame. */
    public long blockIndex(Tier t) {
        return (subframe - 1) / t.blockSpan;
    }
}
