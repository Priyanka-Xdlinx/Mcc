package com.orblinx.mcc.decode;

import com.orblinx.mcc.wire.Tier;
import java.util.List;

/**
 * One lookup-table entry: a telecommand the OBC replays on its tier's schedule,
 * and the byte runs it keeps from the reply.
 *
 * Position within a tier is the ONLY link between what the OBC transmits and
 * what the ground decodes - there are no names or ids on the wire.
 */
public record EntryDef(
        int id,
        Tier tier,
        int entryIndex,
        String tcId,
        String subsystem,
        String rsp,
        Integer apid,
        int tmRawLen,
        Integer messageId,
        List<PickDef> picks) {
}
