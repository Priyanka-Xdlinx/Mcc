package com.orblinx.mcc.auth;

import java.nio.charset.StandardCharsets;
import java.util.Locale;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * Length over composition rules (NIST SP 800-63B): at least 12 characters, not
 * the username, not a well-known password.
 *
 * The 72-byte cap is BCrypt's: it silently ignores everything after byte 72, so
 * a longer password would only look stronger than it is.
 */
public final class PasswordPolicy {

    public static final int MIN_LENGTH = 12;
    public static final int MAX_BYTES = 72;

    private static final Pattern USERNAME = Pattern.compile("[A-Za-z0-9._-]{3,40}");
    private static final Set<String> COMMON = Set.of(
            "password1234", "123456789012", "qwertyuiop12", "administrator", "changeme1234",
            "letmein12345", "welcome12345", "password@123", "passw0rd1234", "iloveyou1234",
            "orblinxmcc123", "missioncontrol", "xdlinx123456", "qwerty123456", "admin1234567");

    private PasswordPolicy() {
    }

    /** @return why the username is unusable, or null when it is fine */
    public static String checkUsername(String username) {
        if (username == null || !USERNAME.matcher(username).matches()) {
            return "Username must be 3-40 characters: letters, digits, '.', '_' or '-'";
        }
        return null;
    }

    /** @return why the password is unacceptable, or null when it is fine */
    public static String check(String password, String username) {
        if (password == null || password.codePointCount(0, password.length()) < MIN_LENGTH) {
            return "Password must be at least " + MIN_LENGTH + " characters";
        }
        if (password.getBytes(StandardCharsets.UTF_8).length > MAX_BYTES) {
            return "Password must be at most " + MAX_BYTES + " bytes";
        }
        if (password.isBlank() || password.chars().distinct().count() < 4) {
            return "Password is too simple";
        }
        String lower = password.toLowerCase(Locale.ROOT);
        if (username != null && !username.isBlank() && lower.contains(username.toLowerCase(Locale.ROOT))) {
            return "Password must not contain the username";
        }
        if (COMMON.contains(lower)) return "That password is too common";
        return null;
    }
}
