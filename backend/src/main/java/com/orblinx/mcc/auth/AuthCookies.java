package com.orblinx.mcc.auth;

import java.time.Duration;
import java.util.List;

import jakarta.servlet.http.Cookie;
import jakarta.servlet.http.HttpServletRequest;

import org.springframework.http.ResponseCookie;
import org.springframework.stereotype.Component;

/**
 * The session cookies. Both are HttpOnly, so script on the page can never read
 * a token, and SameSite=Strict, so no other site can make the browser send
 * them, which also makes a cross-site WebSocket to /ws/telemetry harmless.
 *
 * The refresh token is scoped to /auth: it is only ever sent to the three
 * endpoints that need it, never with ordinary API traffic.
 */
@Component
public class AuthCookies {

    public static final String REFRESH_COOKIE = "refresh_token";
    public static final String ACCESS_COOKIE = "access_token";

    static final String REFRESH_PATH = "/auth";
    static final String ACCESS_PATH = "/";
    static final String SAME_SITE = "Strict";

    private final boolean secure;
    private final Duration refreshTtl;
    private final Duration accessTtl;

    public AuthCookies(AuthProperties properties) {
        this.secure = properties.secureCookies();
        this.refreshTtl = properties.refreshTtl();
        this.accessTtl = properties.accessTtl();
    }

    public ResponseCookie refresh(String value) {
        return cookie(REFRESH_COOKIE, value, REFRESH_PATH, refreshTtl);
    }

    public ResponseCookie access(String value) {
        return cookie(ACCESS_COOKIE, value, ACCESS_PATH, accessTtl);
    }

    public List<ResponseCookie> clearSession() {
        return List.of(cookie(REFRESH_COOKIE, "", REFRESH_PATH, Duration.ZERO),
                       cookie(ACCESS_COOKIE, "", ACCESS_PATH, Duration.ZERO));
    }

    public static String read(HttpServletRequest request, String name) {
        Cookie[] cookies = request.getCookies();
        if (cookies == null) return null;
        for (Cookie cookie : cookies) {
            if (name.equals(cookie.getName()) && cookie.getValue() != null && !cookie.getValue().isBlank()) {
                return cookie.getValue();
            }
        }
        return null;
    }

    private ResponseCookie cookie(String name, String value, String path, Duration maxAge) {
        return ResponseCookie.from(name, value)
                .httpOnly(true)
                .secure(secure)
                .sameSite(SAME_SITE)
                .path(path)
                .maxAge(maxAge)
                .build();
    }
}
