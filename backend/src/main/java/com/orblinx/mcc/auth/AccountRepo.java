package com.orblinx.mcc.auth;

import com.orblinx.mcc.MccApplication;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.util.Base64;
import java.util.List;
import java.util.Optional;
import javax.sql.DataSource;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.dao.DataAccessException;
import org.springframework.dao.DuplicateKeyException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Repository;

/**
 * Accounts, sign-in sessions and the token signing keys, in their own SQLite
 * file (mcc-auth.db) apart from mcc.db: a leaked copy of one never exposes
 * the other, and wiping it to start over never touches telemetry or pages.
 *
 * A session row is what makes a token revocable: every refresh token, and
 * every access token, names a session, and is only honoured while that row is
 * live and the account's token_version still matches. Sign-out, disabling an
 * account and changing a password therefore take effect on the next request.
 */
@Repository
public class AccountRepo {

    public enum Role { ADMIN, OPERATOR }

    public record Account(long id, String username, String displayName, String passwordHash, Role role,
                          boolean enabled, boolean mustChangePassword, int failedLogins, Instant lockedUntil,
                          int tokenVersion, String createdAt, String lastLoginAt) {

        public boolean lockedAt(Instant now) {
            return lockedUntil != null && lockedUntil.isAfter(now);
        }
    }

    private static final String DDL = """
            CREATE TABLE IF NOT EXISTS account (
              id                   INTEGER PRIMARY KEY,
              username             TEXT NOT NULL UNIQUE COLLATE NOCASE,
              display_name         TEXT NOT NULL,
              password_hash        TEXT NOT NULL,
              role                 TEXT NOT NULL CHECK (role IN ('ADMIN', 'OPERATOR')),
              enabled              INTEGER NOT NULL DEFAULT 1,
              must_change_password INTEGER NOT NULL DEFAULT 0,
              failed_logins        INTEGER NOT NULL DEFAULT 0,
              locked_until         INTEGER,
              token_version        INTEGER NOT NULL DEFAULT 0,
              created_at           TEXT NOT NULL,
              updated_at           TEXT NOT NULL,
              last_login_at        TEXT
            );
            CREATE TABLE IF NOT EXISTS auth_session (
              id          TEXT PRIMARY KEY,
              account_id  INTEGER NOT NULL REFERENCES account(id) ON DELETE CASCADE,
              created_at  TEXT NOT NULL,
              expires_at  INTEGER NOT NULL,
              last_used_at TEXT NOT NULL,
              revoked_at  INTEGER,
              ip          TEXT,
              user_agent  TEXT
            );
            CREATE TABLE IF NOT EXISTS auth_key (
              name   TEXT PRIMARY KEY,
              secret TEXT NOT NULL
            );""";

    private static final String ACCOUNT_SELECT = """
            SELECT id, username, display_name, password_hash, role, enabled, must_change_password,
                   failed_logins, locked_until, token_version, created_at, last_login_at FROM account""";

    private static final RowMapper<Account> ACCOUNT_ROW = (ResultSet rs, int i) -> new Account(
            rs.getLong("id"), rs.getString("username"), rs.getString("display_name"),
            rs.getString("password_hash"), Role.valueOf(rs.getString("role")), rs.getBoolean("enabled"),
            rs.getBoolean("must_change_password"), rs.getInt("failed_logins"),
            millis(rs, "locked_until"), rs.getInt("token_version"), rs.getString("created_at"),
            rs.getString("last_login_at"));

    private final JdbcTemplate jdbc;

    public AccountRepo(@Value("${mcc.auth.db}") String db) {
        this.jdbc = new JdbcTemplate(dataSource(db));
        for (String stmt : DDL.split(";")) {
            if (!stmt.isBlank()) jdbc.execute(stmt);
        }
    }

    /** Package-visible so a test can point this repository at a throwaway file. */
    static DataSource dataSource(String db) {
        return MccApplication.sqliteDataSource(db);
    }

    // ── signing keys ────────────────────────────────────────────────────────

    /**
     * The named HMAC key, created on first use. Kept in the database so sessions
     * survive a restart without anyone having to configure a secret.
     */
    public byte[] signingKey(String name) {
        jdbc.update("INSERT INTO auth_key (name, secret) VALUES (?, ?) ON CONFLICT(name) DO NOTHING",
                    name, Base64.getEncoder().encodeToString(SecureValues.randomBytes(32)));
        String secret = jdbc.queryForObject("SELECT secret FROM auth_key WHERE name = ?", String.class, name);
        return Base64.getDecoder().decode(secret);
    }

    // ── accounts ────────────────────────────────────────────────────────────

    public long count() {
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM account", Long.class);
        return n == null ? 0 : n;
    }

    public long enabledAdmins() {
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM account WHERE role = 'ADMIN' AND enabled", Long.class);
        return n == null ? 0 : n;
    }

    public List<Account> all() {
        return jdbc.query(ACCOUNT_SELECT + " ORDER BY lower(username)", ACCOUNT_ROW);
    }

    public Optional<Account> byId(long id) {
        return jdbc.query(ACCOUNT_SELECT + " WHERE id = ?", ACCOUNT_ROW, id).stream().findFirst();
    }

    /** Case-insensitive, matching the unique index on username (COLLATE NOCASE). */
    public Optional<Account> byUsername(String username) {
        return jdbc.query(ACCOUNT_SELECT + " WHERE lower(username) = lower(?)", ACCOUNT_ROW, username)
                   .stream().findFirst();
    }

    /** @throws DuplicateKeyException when the username (case-insensitively) is taken */
    public Account create(String username, String displayName, String passwordHash, Role role,
                          boolean mustChangePassword) {
        String now = Instant.now().toString();
        Long id;
        try {
            id = jdbc.queryForObject("""
                    INSERT INTO account (username, display_name, password_hash, role, must_change_password,
                                         created_at, updated_at) VALUES (?, ?, ?, ?, ?, ?, ?) RETURNING id""",
                    Long.class, username, displayName, passwordHash, role.name(), mustChangePassword, now, now);
        } catch (DataAccessException e) {
            throw clash(e);
        }
        return byId(id).orElseThrow();
    }

    /**
     * Counts a failed password; at {@code maxFailures} the account locks until
     * {@code lockUntil}. A lock that has run out starts the count again, so one
     * slip after a lockout does not re-lock at once.
     */
    public void recordFailedLogin(long id, int maxFailures, Instant lockUntil) {
        long now = System.currentTimeMillis();
        jdbc.update("""
                UPDATE account SET
                  failed_logins = CASE WHEN locked_until <= ? THEN 1 ELSE failed_logins + 1 END,
                  locked_until  = CASE WHEN (CASE WHEN locked_until <= ? THEN 1 ELSE failed_logins + 1 END) >= ?
                                       THEN ?
                                       WHEN locked_until <= ? THEN NULL
                                       ELSE locked_until END
                 WHERE id = ?""", now, now, maxFailures, lockUntil.toEpochMilli(), now, id);
    }

    public void recordLogin(long id) {
        jdbc.update("UPDATE account SET failed_logins = 0, locked_until = NULL, last_login_at = ? WHERE id = ?",
                    Instant.now().toString(), id);
    }

    /** Also bumps token_version, which ends every session's tokens. */
    public void setPassword(long id, String passwordHash, boolean mustChange) {
        jdbc.update("""
                UPDATE account SET password_hash = ?, must_change_password = ?, failed_logins = 0,
                       locked_until = NULL, token_version = token_version + 1, updated_at = ? WHERE id = ?""",
                passwordHash, mustChange, Instant.now().toString(), id);
    }

    /** Role and enabled changes bump token_version, so they apply to live sessions at once. */
    public void update(long id, String displayName, Role role, boolean enabled) {
        jdbc.update("""
                UPDATE account SET display_name = ?, role = ?, enabled = ?, failed_logins = 0,
                       locked_until = NULL, token_version = token_version + 1, updated_at = ? WHERE id = ?""",
                displayName, role.name(), enabled, Instant.now().toString(), id);
    }

    public void delete(long id) {
        jdbc.update("DELETE FROM account WHERE id = ?", id);
    }

    // ── sessions ────────────────────────────────────────────────────────────

    public String createSession(long accountId, Instant expiresAt, String ip, String userAgent) {
        String id = SecureValues.random32();
        String now = Instant.now().toString();
        jdbc.update("""
                INSERT INTO auth_session (id, account_id, created_at, expires_at, last_used_at, ip, user_agent)
                VALUES (?, ?, ?, ?, ?, ?, ?)""",
                id, accountId, now, expiresAt.toEpochMilli(), now, truncate(ip, 64), truncate(userAgent, 256));
        return id;
    }

    /** True while the session is unrevoked, unexpired and belongs to an enabled account at this token version. */
    public boolean sessionLive(String sessionId, long accountId, int tokenVersion) {
        Boolean live = jdbc.queryForObject("""
                SELECT EXISTS (SELECT 1 FROM auth_session s JOIN account a ON a.id = s.account_id
                 WHERE s.id = ? AND s.account_id = ? AND s.revoked_at IS NULL AND s.expires_at > ?
                   AND a.enabled AND a.token_version = ?)""",
                Boolean.class, sessionId, accountId, System.currentTimeMillis(), tokenVersion);
        return Boolean.TRUE.equals(live);
    }

    public void touchSession(String sessionId) {
        jdbc.update("UPDATE auth_session SET last_used_at = ? WHERE id = ?", Instant.now().toString(), sessionId);
    }

    public void revokeSession(String sessionId) {
        jdbc.update("UPDATE auth_session SET revoked_at = ? WHERE id = ? AND revoked_at IS NULL",
                    System.currentTimeMillis(), sessionId);
    }

    /** Every session of the account except {@code keep} (null = all). */
    public void revokeSessions(long accountId, String keep) {
        jdbc.update("""
                UPDATE auth_session SET revoked_at = ?
                 WHERE account_id = ? AND revoked_at IS NULL AND id IS NOT ?""",
                System.currentTimeMillis(), accountId, keep);
    }

    /** Drops sessions that ended over a day ago; called on sign-in so the table stays small. */
    public void pruneSessions() {
        long cutoff = System.currentTimeMillis() - 24 * 3600_000L;
        jdbc.update("DELETE FROM auth_session WHERE expires_at < ? OR revoked_at < ?", cutoff, cutoff);
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    private static Instant millis(ResultSet rs, String column) throws SQLException {
        long v = rs.getLong(column);
        return rs.wasNull() ? null : Instant.ofEpochMilli(v);
    }

    private static String truncate(String v, int max) {
        return v == null || v.length() <= max ? v : v.substring(0, max);
    }

    /**
     * SQLite reports a UNIQUE violation as SQLITE_CONSTRAINT_UNIQUE; Spring does
     * not always translate that to DuplicateKeyException, so look at the cause.
     */
    private static RuntimeException clash(DataAccessException e) {
        for (Throwable t = e; t != null; t = t.getCause()) {
            if (t instanceof org.sqlite.SQLiteException se
                    && se.getResultCode() == org.sqlite.SQLiteErrorCode.SQLITE_CONSTRAINT_UNIQUE) {
                return new DuplicateKeyException("username is taken", e);
            }
        }
        return e;
    }
}
