package com.orblinx.mcc.decode;

/**
 * Why a decoded value is or is not trustworthy.
 *
 * The wire carries sentinels that a plain numeric column cannot express, and
 * conflating them with real readings is how a ground station ends up showing a
 * confident, plausible, wrong number.
 */
public enum Quality {
    /** Decoded normally. */
    OK,
    /** Every byte of the field was 0xFF: the subsystem did not answer. Not a reading of 255. */
    NO_ANSWER,
    /** No pick claimed these bytes - they are zero fill that was never downlinked. */
    NOT_DOWNLINKED,
    /** A subframe slice of this tier block never arrived, so the block is stale. */
    BLOCK_INCOMPLETE,
    /** First master frame after OBC boot - pick ranges are still 0xFF-filled. */
    WARMUP
}
