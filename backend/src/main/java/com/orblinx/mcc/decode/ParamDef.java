package com.orblinx.mcc.decode;

/**
 * One parameter definition, as stored in the schema.
 *
 * {@code scale} is ALWAYS a multiplier: engineering = raw * scale. The MIB JSON
 * stores it that way; only the flattened CSV export flips it for ADCS, which is
 * why this app never reads that CSV.
 */
public record ParamDef(
        int id,
        int messageId,
        int seq,
        String name,
        Integer offset,        // in the owning message's offset unit
        int length,            // in the owning message's offset unit
        Integer bitOffset,     // normalised: absolute bits from payload start
        Integer bitLength,
        boolean byteAligned,
        String dataType,
        boolean bigEndian,
        Double scale,
        String unit,
        Integer enumTypeId,
        boolean isArray) {
}
