package com.orblinx.mcc.auth;

import java.util.Map;

import jakarta.servlet.http.HttpServletRequest;

import com.fasterxml.jackson.annotation.JsonInclude;
import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;
import org.springframework.http.CacheControl;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseCookie;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.oauth2.jwt.Jwt;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RestController;
import org.springframework.web.bind.annotation.RestControllerAdvice;

/**
 * Sign-in with a username and password:
 * <ol>
 *   <li>POST /auth/login {username, password} sets access_token + refresh_token</li>
 *   <li>/api/** and /ws/** authenticate from the access_token cookie (15 min)</li>
 *   <li>POST /auth/refresh renews it from refresh_token while the session lives</li>
 *   <li>POST /auth/logout revokes the session and clears both cookies</li>
 * </ol>
 * Every write, sign-in included, carries the X-XSRF-TOKEN header (see SecurityConfig);
 * GET /auth/csrf hands out the cookie it is copied from.
 */
@RestController
@Tag(name = "Authentication", description = "Username/password sign-in with JWT session cookies")
public class AuthController {

    private final AuthService auth;
    private final AuthCookies cookies;
    private final LoginThrottle throttle;

    public AuthController(AuthService auth, AuthCookies cookies, LoginThrottle throttle) {
        this.auth = auth;
        this.cookies = cookies;
        this.throttle = throttle;
    }

    public record LoginRequest(String username, String password) {
    }

    public record PasswordChange(String currentPassword, String newPassword) {
    }

    @JsonInclude(JsonInclude.Include.NON_NULL)
    public record SessionResponse(AuthService.UserView user, Long expiresIn) {
    }

    /** Sets the XSRF-TOKEN cookie (every response does; this one exists only to be called first). */
    @GetMapping("/auth/csrf")
    @Operation(summary = "CSRF cookie", description = "204; sets the XSRF-TOKEN cookie that writes must echo.")
    public ResponseEntity<Void> csrf() {
        return ResponseEntity.noContent().cacheControl(CacheControl.noStore()).build();
    }

    @PostMapping(path = "/auth/login", consumes = MediaType.APPLICATION_JSON_VALUE)
    @Operation(summary = "Sign in", description = "{username, password}. 200 {user} and session cookies; "
            + "401 for any bad credentials; 429 after too many attempts from one address.")
    public ResponseEntity<SessionResponse> login(@RequestBody LoginRequest body, HttpServletRequest request) {
        String ip = request.getRemoteAddr();
        if (!throttle.tryAcquire(ip)) {
            throw new AuthException(HttpStatus.TOO_MANY_REQUESTS,
                    "Too many sign-in attempts. Try again in " + throttle.retryAfterSeconds(ip) + " seconds.");
        }
        AuthService.Session s = auth.login(body == null ? null : body.username(),
                body == null ? null : body.password(), ip, request.getHeader(HttpHeaders.USER_AGENT));
        return withSession(s);
    }

    @PostMapping("/auth/refresh")
    @Operation(summary = "Refresh", description = "Renews access_token from the refresh_token cookie. "
            + "401 (and both cookies cleared) when the session has ended.")
    public ResponseEntity<SessionResponse> refresh(HttpServletRequest request) {
        try {
            return withSession(auth.refresh(AuthCookies.read(request, AuthCookies.REFRESH_COOKIE)));
        } catch (AuthException ex) {
            ResponseEntity.BodyBuilder b = ResponseEntity.status(ex.status()).cacheControl(CacheControl.noStore());
            for (ResponseCookie c : cookies.clearSession()) b.header(HttpHeaders.SET_COOKIE, c.toString());
            throw new CookieClearingException(ex, b);
        }
    }

    @PostMapping("/auth/logout")
    @Operation(summary = "Sign out", description = "Revokes this session and clears the cookies.")
    public ResponseEntity<Void> logout(HttpServletRequest request) {
        auth.logout(AuthCookies.read(request, AuthCookies.REFRESH_COOKIE));
        ResponseEntity.HeadersBuilder<?> b = ResponseEntity.noContent().cacheControl(CacheControl.noStore());
        for (ResponseCookie c : cookies.clearSession()) b.header(HttpHeaders.SET_COOKIE, c.toString());
        return b.build();
    }

    @GetMapping("/api/me")
    @Operation(summary = "Current user", description = "{id, username, displayName, role, mustChangePassword}")
    public AuthService.UserView me(@AuthenticationPrincipal Jwt jwt) {
        return auth.me(claims(jwt).accountId());
    }

    @PostMapping(path = "/api/me/password", consumes = MediaType.APPLICATION_JSON_VALUE)
    @Operation(summary = "Change my password", description = "{currentPassword, newPassword}. Signs out every "
            + "other session of this account; this one gets fresh cookies.")
    public ResponseEntity<SessionResponse> changePassword(@AuthenticationPrincipal Jwt jwt,
                                                          @RequestBody PasswordChange body) {
        if (body == null) throw new AuthException(HttpStatus.BAD_REQUEST, "Body required");
        return withSession(auth.changePassword(claims(jwt), body.currentPassword(), body.newPassword()));
    }

    static AppTokenService.Claims claims(Jwt jwt) {
        return AppTokenService.claimsOf(jwt)
                .orElseThrow(() -> new AuthException(HttpStatus.UNAUTHORIZED, "Not signed in"));
    }

    private ResponseEntity<SessionResponse> withSession(AuthService.Session s) {
        ResponseEntity.BodyBuilder b = ResponseEntity.ok().cacheControl(CacheControl.noStore())
                .header(HttpHeaders.SET_COOKIE, cookies.access(s.accessToken()).toString());
        if (s.refreshToken() != null) b.header(HttpHeaders.SET_COOKIE, cookies.refresh(s.refreshToken()).toString());
        return b.body(new SessionResponse(s.user(), auth.accessTtlSeconds()));
    }

    /** An AuthException whose response must also clear the session cookies. */
    static final class CookieClearingException extends RuntimeException {

        final AuthException cause;
        final ResponseEntity.BodyBuilder response;

        CookieClearingException(AuthException cause, ResponseEntity.BodyBuilder response) {
            super(cause.getMessage(), null, false, false);
            this.cause = cause;
            this.response = response;
        }
    }

    /** Auth refusals answer {error}, like the rest of the API. */
    @RestControllerAdvice(assignableTypes = {AuthController.class, AccountController.class})
    static class Errors {

        @ExceptionHandler(AuthException.class)
        ResponseEntity<Map<String, String>> auth(AuthException ex) {
            return ResponseEntity.status(ex.status()).cacheControl(CacheControl.noStore())
                    .body(Map.of("error", ex.getMessage()));
        }

        @ExceptionHandler(CookieClearingException.class)
        ResponseEntity<Map<String, String>> clearing(CookieClearingException ex) {
            return ex.response.body(Map.of("error", ex.getMessage()));
        }

        @ExceptionHandler(org.springframework.http.converter.HttpMessageNotReadableException.class)
        ResponseEntity<Map<String, String>> unreadable() {
            return ResponseEntity.badRequest().body(Map.of("error", "Malformed JSON body"));
        }
    }
}
