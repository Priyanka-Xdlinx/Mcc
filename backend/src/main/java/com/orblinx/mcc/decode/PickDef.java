package com.orblinx.mcc.decode;

import java.util.List;

/**
 * One contiguous byte run the OBC copies out of a subsystem reply into the tier
 * block.
 *
 * {@code block} is an ABSOLUTE offset inside the assembled tier block. Reading
 * the block sequentially entry by entry happens to work only while entries are
 * laid out contiguously, and silently breaks when they are not.
 */
public record PickDef(int id, int seq, int src, int size, int block, List<String> paramNames) {
}
