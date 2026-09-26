package com.orblinx.mcc.decode;

import java.util.List;
import java.util.Map;

/**
 * A message definition plus its parameters, indexed by name.
 *
 * ADCS and XLINK-S messages are bit-addressed ({@code offsetUnit = "bits"});
 * the rest are byte-addressed. Both the offset and the length are expressed in
 * that unit.
 */
public record MessageDef(
        int id,
        String subsystem,
        String name,
        String direction,
        String offsetUnit,
        Integer lengthBytes,
        String node,
        Integer msgId,
        List<ParamDef> parameters,
        Map<String, ParamDef> byName) {

    public boolean bitAddressed() {
        return "bits".equals(offsetUnit);
    }

    /**
     * The dictionary's key for a parameter: subsystem.node.msg_id.direction.seq.
     *
     * EPS runs three physical nodes (PDU1, BMS1, MPPT1) that each restart their
     * own message numbering at 1, so neither msg_id nor name alone is unique.
     */
    public String parameterUid(int seq) {
        return String.join(".", subsystem, node == null ? "default" : node,
                           msgId == null ? "?" : String.valueOf(msgId), direction,
                           String.valueOf(seq));
    }
}
