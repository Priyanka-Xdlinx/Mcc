package com.orblinx.mcc.auth;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.List;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.http.HttpStatus;
import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;
import org.springframework.stereotype.Service;

import com.orblinx.mcc.auth.AccountRepo.Account;
import com.orblinx.mcc.auth.AccountRepo.Role;

/**
 * Sign-in, sessions and account management, free of HTTP concerns: the
 * controllers pass in raw values and turn the results into cookies and JSON.
 *
 * Every sign-in failure answers the same "Invalid username or password",
 * whether the name is unknown, the password wrong, the account disabled or
 * locked: anything more specific tells a guesser which names exist.
 */
@Service
public class AuthService {

    /** Audit trail of sign-ins and account changes, separate from the decoder's logging. */
    private static final Logger audit = LoggerFactory.getLogger("mcc.audit");
    private static final Logger log = LoggerFactory.getLogger(AuthService.class);
    private static final String INVALID = "Invalid username or password";
    static final String BOOTSTRAP_FILE = "mcc-admin-password.txt";

    private final AuthProperties properties;
    private final AccountRepo repo;
    private final AppTokenService tokens;
    private final BCryptPasswordEncoder encoder = new BCryptPasswordEncoder(12);
    /** Compared against when the username is unknown, so both cases take a BCrypt's time. */
    private final String dummyHash;

    /** Where the first admin's one-time password file is written: mcc.data-dir. */
    private final Path dataDir;

    public AuthService(AuthProperties properties, AccountRepo repo, AppTokenService tokens,
                       @Value("${mcc.data-dir}") String dataDir) {
        this.properties = properties;
        this.dataDir = Path.of(dataDir).toAbsolutePath().normalize();
        this.repo = repo;
        this.tokens = tokens;
        this.dummyHash = encoder.encode(SecureValues.random32());
        bootstrapAdmin();
    }

    /** The signed-in user as the frontend sees it. */
    public record UserView(long id, String username, String displayName, String role, boolean mustChangePassword,
                           List<String> permissions) {

        static UserView of(Account a) {
            return new UserView(a.id(), a.username(), a.displayName(), a.role().name(), a.mustChangePassword(),
                                Permission.names(Permission.grantedTo(a)));
        }
    }

    /** An account as the admin page sees it. */
    public record AccountView(long id, String username, String displayName, String role, boolean enabled,
                              boolean locked, boolean mustChangePassword, String createdAt, String lastLoginAt) {

        static AccountView of(Account a) {
            return new AccountView(a.id(), a.username(), a.displayName(), a.role().name(), a.enabled(),
                                   a.lockedAt(Instant.now()), a.mustChangePassword(), a.createdAt(), a.lastLoginAt());
        }
    }

    /** Credentials for the cookies, plus the user for the response body. */
    public record Session(UserView user, String accessToken, String refreshToken) {
    }

    // ── sign-in ─────────────────────────────────────────────────────────────

    public Session login(String username, String password, String ip, String userAgent) {
        if (username == null || password == null || username.isBlank() || password.isEmpty()
                || username.length() > 64
                // BCrypt ignores bytes past 72, so a longer "password" would match its own prefix.
                || password.getBytes(StandardCharsets.UTF_8).length > PasswordPolicy.MAX_BYTES) {
            encoder.matches("x", dummyHash);
            throw new AuthException(HttpStatus.UNAUTHORIZED, INVALID);
        }
        Account account = repo.byUsername(username.trim()).orElse(null);
        if (account == null) {
            encoder.matches(password, dummyHash);
            audit.warn("login failed: unknown username={} ip={}", sanitize(username), ip);
            throw new AuthException(HttpStatus.UNAUTHORIZED, INVALID);
        }
        Instant now = Instant.now();
        boolean passwordOk = encoder.matches(password, account.passwordHash());
        if (account.lockedAt(now)) {
            audit.warn("login refused: account locked user={} ip={}", account.username(), ip);
            throw new AuthException(HttpStatus.UNAUTHORIZED, INVALID);
        }
        if (!passwordOk) {
            repo.recordFailedLogin(account.id(), properties.maxFailedLogins(), now.plus(properties.lockout()));
            boolean lockRanOut = account.lockedUntil() != null;
            int failures = lockRanOut ? 1 : account.failedLogins() + 1;
            audit.warn("login failed: bad password user={} ip={} failures={}{}", account.username(), ip, failures,
                       failures >= properties.maxFailedLogins() ? " -> locked for " + properties.lockout() : "");
            throw new AuthException(HttpStatus.UNAUTHORIZED, INVALID);
        }
        if (!account.enabled()) {
            audit.warn("login refused: account disabled user={} ip={}", account.username(), ip);
            throw new AuthException(HttpStatus.UNAUTHORIZED, INVALID);
        }
        repo.recordLogin(account.id());
        repo.pruneSessions();
        Instant expiry = now.plus(properties.refreshTtl());
        String sessionId = repo.createSession(account.id(), expiry, ip, userAgent);
        audit.info("login ok: user={} ip={}", account.username(), ip);
        return new Session(UserView.of(account), tokens.issueAccessToken(account, sessionId),
                           tokens.issueRefreshToken(account, sessionId, expiry));
    }

    /**
     * A new access token for a live session. The account is re-read, so a role
     * change or a disabled account shows up here.
     * @return the session with a null refresh token (the cookie is kept as is)
     */
    public Session refresh(String refreshToken) {
        AppTokenService.Claims claims = tokens.verifyRefreshToken(refreshToken).orElseThrow(AuthService::noSession);
        if (!repo.sessionLive(claims.sessionId(), claims.accountId(), claims.tokenVersion())) throw noSession();
        Account account = repo.byId(claims.accountId()).orElseThrow(AuthService::noSession);
        repo.touchSession(claims.sessionId());
        return new Session(UserView.of(account), tokens.issueAccessToken(account, claims.sessionId()), null);
    }

    public void logout(String refreshToken) {
        tokens.verifyRefreshToken(refreshToken).ifPresent(c -> {
            repo.revokeSession(c.sessionId());
            repo.byId(c.accountId()).ifPresent(a -> audit.info("logout: user={}", a.username()));
        });
    }

    /** True while an access token's session is still honoured; checked on every API request. */
    public boolean accessTokenLive(AppTokenService.Claims claims) {
        return repo.sessionLive(claims.sessionId(), claims.accountId(), claims.tokenVersion());
    }

    public long accessTtlSeconds() {
        return properties.accessTtl().toSeconds();
    }

    public UserView me(long accountId) {
        return repo.byId(accountId).map(UserView::of).orElseThrow(AuthService::noSession);
    }

    /**
     * Changes the caller's own password. Every other session of the account is
     * signed out; this one is re-issued tokens at the new token version.
     */
    public Session changePassword(AppTokenService.Claims caller, String current, String next) {
        Account account = repo.byId(caller.accountId()).orElseThrow(AuthService::noSession);
        if (current == null || current.getBytes(StandardCharsets.UTF_8).length > PasswordPolicy.MAX_BYTES
                || !encoder.matches(current, account.passwordHash())) {
            audit.warn("password change refused: wrong current password user={}", account.username());
            throw new AuthException(HttpStatus.BAD_REQUEST, "Current password is incorrect");
        }
        String problem = PasswordPolicy.check(next, account.username());
        if (problem != null) throw new AuthException(HttpStatus.BAD_REQUEST, problem);
        if (encoder.matches(next, account.passwordHash())) {
            throw new AuthException(HttpStatus.BAD_REQUEST, "New password must differ from the current one");
        }
        repo.setPassword(account.id(), encoder.encode(next), false);
        repo.revokeSessions(account.id(), caller.sessionId());
        audit.info("password changed: user={}", account.username());
        deleteBootstrapFileFor(account);
        Account updated = repo.byId(account.id()).orElseThrow();
        Instant expiry = Instant.now().plus(properties.refreshTtl());
        return new Session(UserView.of(updated), tokens.issueAccessToken(updated, caller.sessionId()),
                           tokens.issueRefreshToken(updated, caller.sessionId(), expiry));
    }

    // ── account management (admins) ─────────────────────────────────────────

    public List<AccountView> accounts() {
        return repo.all().stream().map(AccountView::of).toList();
    }

    public AccountView create(long adminId, String username, String displayName, String role, String password) {
        String problem = PasswordPolicy.checkUsername(username);
        if (problem == null) problem = PasswordPolicy.check(password, username);
        if (problem != null) throw new AuthException(HttpStatus.BAD_REQUEST, problem);
        Role r = role(role);
        try {
            // A password an admin chose is one more than one person knows: change it at first sign-in.
            Account a = repo.create(username, displayName(displayName, username), encoder.encode(password), r, true);
            audit.info("account created: user={} role={} by={}", a.username(), r, adminName(adminId));
            return AccountView.of(a);
        } catch (DuplicateKeyException ex) {
            throw new AuthException(HttpStatus.CONFLICT, "Username '" + username + "' is taken");
        }
    }

    public AccountView update(long adminId, long id, String displayName, String role, Boolean enabled) {
        Account a = repo.byId(id).orElseThrow(() -> new AuthException(HttpStatus.NOT_FOUND, "No such user"));
        Role r = role == null ? a.role() : role(role);
        boolean on = enabled == null ? a.enabled() : enabled;
        boolean losesAdmin = a.role() == Role.ADMIN && a.enabled() && (r != Role.ADMIN || !on);
        if (losesAdmin && repo.enabledAdmins() <= 1) {
            throw new AuthException(HttpStatus.CONFLICT, "There must always be at least one enabled admin");
        }
        if (id == adminId && !on) throw new AuthException(HttpStatus.CONFLICT, "You cannot disable yourself");
        repo.update(id, displayName == null ? a.displayName() : displayName(displayName, a.username()), r, on);
        if (!on) repo.revokeSessions(id, null);
        audit.info("account updated: user={} role={} enabled={} by={}", a.username(), r, on, adminName(adminId));
        return AccountView.of(repo.byId(id).orElseThrow());
    }

    /** Sets a temporary password; the user must change it at next sign-in, and is signed out everywhere. */
    public void resetPassword(long adminId, long id, String password) {
        Account a = repo.byId(id).orElseThrow(() -> new AuthException(HttpStatus.NOT_FOUND, "No such user"));
        String problem = PasswordPolicy.check(password, a.username());
        if (problem != null) throw new AuthException(HttpStatus.BAD_REQUEST, problem);
        repo.setPassword(id, encoder.encode(password), true);
        repo.revokeSessions(id, null);
        audit.info("password reset: user={} by={}", a.username(), adminName(adminId));
    }

    public void delete(long adminId, long id) {
        Account a = repo.byId(id).orElseThrow(() -> new AuthException(HttpStatus.NOT_FOUND, "No such user"));
        if (id == adminId) throw new AuthException(HttpStatus.CONFLICT, "You cannot delete yourself");
        if (a.role() == Role.ADMIN && a.enabled() && repo.enabledAdmins() <= 1) {
            throw new AuthException(HttpStatus.CONFLICT, "There must always be at least one enabled admin");
        }
        repo.delete(id);
        audit.info("account deleted: user={} by={}", a.username(), adminName(adminId));
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    /**
     * With no accounts at all, creates the first admin. Its password comes from
     * mcc.auth.bootstrap-password or is generated and written to a file in
     * mcc.data-dir (never to the log); either way it must be changed at first
     * sign-in.
     */
    private void bootstrapAdmin() {
        if (repo.count() > 0) return;
        String username = properties.bootstrapAdmin();
        String password = properties.bootstrapPassword();
        boolean generated = password == null || password.isBlank();
        if (generated) password = SecureValues.readablePassword(20);
        repo.create(username, "Administrator", encoder.encode(password), Role.ADMIN, true);
        if (generated) {
            Path file = dataDir.resolve(BOOTSTRAP_FILE);
            try {
                Files.writeString(file, "OrbLinx MCC first sign-in\n\nusername: " + username + "\npassword: "
                        + password + "\n\nYou will be asked to choose a new password. This file is deleted "
                        + "once you have.\n");
                log.warn("Created the first admin account '{}'. Its one-time password is in {}", username, file);
            } catch (IOException ex) {
                throw new IllegalStateException("Could not write the first admin password to " + file, ex);
            }
        } else {
            log.warn("Created the first admin account '{}' with mcc.auth.bootstrap-password", username);
        }
        audit.info("account created: user={} role=ADMIN by=bootstrap", username);
    }

    private void deleteBootstrapFileFor(Account account) {
        if (!account.username().equalsIgnoreCase(properties.bootstrapAdmin())) return;
        try {
            Files.deleteIfExists(dataDir.resolve(BOOTSTRAP_FILE));
        } catch (IOException ex) {
            log.warn("Could not delete {}: {}", BOOTSTRAP_FILE, ex.getMessage());
        }
    }

    private static Role role(String role) {
        try {
            return Role.valueOf(role == null ? "" : role.trim().toUpperCase(java.util.Locale.ROOT));
        } catch (IllegalArgumentException ex) {
            throw new AuthException(HttpStatus.BAD_REQUEST, "Role must be ADMIN or OPERATOR");
        }
    }

    private static String displayName(String displayName, String fallback) {
        String d = displayName == null ? "" : displayName.strip();
        if (d.length() > 60) throw new AuthException(HttpStatus.BAD_REQUEST, "Display name must be at most 60 characters");
        return d.isEmpty() ? fallback : d;
    }

    private String adminName(long adminId) {
        return repo.byId(adminId).map(Account::username).orElse("#" + adminId);
    }

    private static AuthException noSession() {
        return new AuthException(HttpStatus.UNAUTHORIZED, "Not signed in");
    }

    private static String sanitize(String value) {
        String cleaned = value.replaceAll("[^A-Za-z0-9_.@-]", "?");
        return cleaned.length() > 64 ? cleaned.substring(0, 64) : cleaned;
    }
}
