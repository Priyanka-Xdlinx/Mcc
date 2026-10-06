package com.orblinx.mcc.auth;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import java.util.Base64;

/** Random values and constant-time comparison. */
final class SecureValues {

    private static final SecureRandom RANDOM = new SecureRandom();
    private static final Base64.Encoder BASE64URL = Base64.getUrlEncoder().withoutPadding();
    /** No 0/O/1/l/I, so a generated password can be typed from a printout. */
    private static final String READABLE = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

    private SecureValues() {
    }

    static byte[] randomBytes(int n) {
        byte[] bytes = new byte[n];
        RANDOM.nextBytes(bytes);
        return bytes;
    }

    /** 32 random bytes, base64url without padding. */
    static String random32() {
        return BASE64URL.encodeToString(randomBytes(32));
    }

    static String readablePassword(int length) {
        StringBuilder sb = new StringBuilder(length);
        for (int i = 0; i < length; i++) sb.append(READABLE.charAt(RANDOM.nextInt(READABLE.length())));
        return sb.toString();
    }

    /** Constant-time equality on UTF-8 bytes; null or empty values never match. */
    static boolean constantTimeEquals(String expected, String actual) {
        if (expected == null || actual == null || expected.isEmpty() || actual.isEmpty()) return false;
        return MessageDigest.isEqual(expected.getBytes(StandardCharsets.UTF_8),
                                     actual.getBytes(StandardCharsets.UTF_8));
    }
}
