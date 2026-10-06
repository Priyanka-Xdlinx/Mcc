package com.orblinx.mcc.auth;

import java.time.Duration;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.boot.context.properties.bind.DefaultValue;

/**
 * Username/password sign-in. Accounts, sessions and the token signing keys
 * live in their own SQLite file, apart from mcc.db.
 *
 * @param db              path to the accounts database (mcc-auth.db)
 * @param secureCookies   Secure flag on the cookies. Browsers accept it on
 *                        http://localhost; set false only to serve plain http
 *                        under another host name.
 * @param bootstrapAdmin  username of the admin created when there are no accounts yet
 * @param bootstrapPassword its password; blank = random, written to
 *                        mcc-admin-password.txt in mcc.data-dir.
 *                        Either way it must be changed at first sign-in.
 */
@ConfigurationProperties("mcc.auth")
public record AuthProperties(String db,
                             @DefaultValue("true") boolean secureCookies,
                             @DefaultValue("admin") String bootstrapAdmin,
                             String bootstrapPassword,
                             @DefaultValue("15m") Duration accessTtl,
                             @DefaultValue("7d") Duration refreshTtl,
                             @DefaultValue("5") int maxFailedLogins,
                             @DefaultValue("15m") Duration lockout,
                             @DefaultValue("20") int loginsPerIp,
                             @DefaultValue("5m") Duration loginWindow) {

    public AuthProperties {
        if (bootstrapAdmin == null || bootstrapAdmin.isBlank()) bootstrapAdmin = "admin";
        if (accessTtl == null) accessTtl = Duration.ofMinutes(15);
        if (refreshTtl == null) refreshTtl = Duration.ofDays(7);
        if (lockout == null) lockout = Duration.ofMinutes(15);
        if (loginWindow == null) loginWindow = Duration.ofMinutes(5);
        if (maxFailedLogins < 1 || loginsPerIp < 1) {
            throw new IllegalStateException("mcc.auth.max-failed-logins and logins-per-ip must be >= 1");
        }
    }
}
