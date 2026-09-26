package com.orblinx.mcc.decode;

/** One decoded parameter reading, ready to store or publish. */
public record DecodedValue(
        Integer parameterId,
        String name,
        Double valueNum,
        String valueText,
        String unit,
        Quality quality,
        String formatted) {
}
