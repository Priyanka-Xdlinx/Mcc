package com.orblinx.mcc;

import com.orblinx.mcc.store.DecodeSchemaLoader;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import javax.sql.DataSource;

import org.springframework.jdbc.core.JdbcTemplate;

/**
 * Two throwaway SQLite files for the whole test run, in a temp directory - and
 * never the developer's own mcc.db / mcc-auth.db. The decode schema is loaded
 * once from the repository's xdos/ tree by the same loader production uses.
 */
public final class TestDb {

    private static Path dir;
    private static DataSource ds;
    private static DataSource authDs;
    private static DecodeSchemaLoader loader;

    private TestDb() { }

    private static synchronized Path dir() {
        if (dir == null) {
            try {
                dir = Files.createTempDirectory("mcc-test-db");
            } catch (IOException e) {
                throw new UncheckedIOException(e);
            }
            dir.toFile().deleteOnExit();
        }
        return dir;
    }

    public static synchronized DataSource dataSource() {
        if (ds == null) ds = MccApplication.sqliteDataSource(dir().resolve("mcc.db").toString());
        return ds;
    }

    public static synchronized DataSource authDataSource() {
        if (authDs == null) authDs = MccApplication.sqliteDataSource(dir().resolve("mcc-auth.db").toString());
        return authDs;
    }

    public static String authDbPath() {
        return dir().resolve("mcc-auth.db").toString();
    }

    public static JdbcTemplate jdbc() {
        return new JdbcTemplate(dataSource());
    }

    public static JdbcTemplate authJdbc() {
        return new JdbcTemplate(authDataSource());
    }

    /** The decode schema, loaded once from xdos/. */
    public static synchronized DecodeSchemaLoader decode() {
        if (loader == null) {
            loader = new DecodeSchemaLoader(jdbc(), Replay.repoRoot().resolve("xdos").toString());
        }
        return loader;
    }

    /**
     * Empties accounts, sessions, keys, display pages and telemetry; the decode
     * schema stays. Tables that have not been created yet (a repository has not
     * been constructed in this JVM) are simply skipped.
     */
    public static void clearUserData() {
        for (String t : List.of("auth_session", "account", "auth_key")) clearIfExists(authJdbc(), t);
        for (String t : List.of("display_page", "account_workspace", "telemetry_latest", "telemetry_history")) {
            clearIfExists(jdbc(), t);
        }
    }

    private static void clearIfExists(JdbcTemplate jdbc, String table) {
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?",
                                     Long.class, table);
        if (n != null && n > 0) jdbc.execute("DELETE FROM " + table);
    }
}
