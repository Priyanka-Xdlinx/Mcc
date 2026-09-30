package com.orblinx.mcc.store;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.util.*;
import javax.sql.DataSource;

import org.springframework.dao.DataAccessException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Repository;

/**
 * Operator-owned display pages and each operator's saved window layout.
 *
 * These tables sit beside the decode schema in mcc.db but are created here, at
 * startup, with CREATE TABLE IF NOT EXISTS: mcc.db already holds telemetry
 * history, and rebuilding it just to add three tiny tables would throw that
 * away. The same DDL is appended to schema/schema.sql so a fresh build has them.
 *
 * Name clashes are left to the UNIQUE constraints rather than checked first, so
 * two operators saving at once get a clean 409 instead of a race.
 */
@Repository
public class DisplayRepo {

    /** A page always shows exactly this many parameter slots: ten rows of two. */
    public static final int SLOTS = 20;

    public record User(long id, String name, int pages) { }

    public record Page(long id, long ownerId, String ownerName, String name,
                       List<Integer> params, String createdAt, String updatedAt) { }

    public record Workspace(String layout, String updatedAt) { }

    /** The owner already has a page by that name. */
    public static final class NameClashException extends RuntimeException {
        public NameClashException(String name) { super("you already have a page named '" + name + "'"); }
    }

    private static final String DDL = """
            CREATE TABLE IF NOT EXISTS app_user (
              id         INTEGER PRIMARY KEY,
              name       TEXT NOT NULL UNIQUE COLLATE NOCASE,
              created_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS display_page (
              id         INTEGER PRIMARY KEY,
              owner_id   INTEGER NOT NULL REFERENCES app_user(id),
              name       TEXT NOT NULL,
              params     TEXT NOT NULL,
              created_at TEXT NOT NULL,
              updated_at TEXT NOT NULL,
              UNIQUE (owner_id, name)
            );
            CREATE TABLE IF NOT EXISTS user_workspace (
              user_id    INTEGER PRIMARY KEY REFERENCES app_user(id),
              layout     TEXT NOT NULL,
              updated_at TEXT NOT NULL
            );""";

    private static final String PAGE_SELECT = """
            SELECT p.id, p.owner_id, u.name AS owner_name, p.name, p.params,
                   p.created_at, p.updated_at
              FROM display_page p JOIN app_user u ON u.id = p.owner_id""";

    private static final TypeReference<List<Integer>> INT_LIST = new TypeReference<>() { };

    private final JdbcTemplate jdbc;
    private final ObjectMapper json = new ObjectMapper();
    /** Parameters the flown LUT downlinks. The LUT is fixed for the process, so load once. */
    private final Set<Integer> downlinked;

    public DisplayRepo(DataSource ds) {
        this.jdbc = new JdbcTemplate(ds);
        for (String stmt : DDL.split(";")) {
            if (!stmt.isBlank()) jdbc.execute(stmt);
        }
        this.downlinked = Set.copyOf(jdbc.queryForList(
                "SELECT DISTINCT parameter_id FROM pick_param WHERE parameter_id IS NOT NULL",
                Integer.class));
    }

    public boolean isDownlinked(int parameterId) { return downlinked.contains(parameterId); }

    // ── users ───────────────────────────────────────────────────────────────

    /** Every operator with a count of their pages, in one query. */
    public List<User> users() {
        return jdbc.query("""
                SELECT u.id, u.name, COUNT(p.id) AS pages
                  FROM app_user u LEFT JOIN display_page p ON p.owner_id = u.id
                 GROUP BY u.id ORDER BY u.name COLLATE NOCASE""", USER_ROW);
    }

    public Optional<User> user(long id) {
        return jdbc.query("""
                SELECT u.id, u.name, (SELECT COUNT(*) FROM display_page WHERE owner_id = u.id) AS pages
                  FROM app_user u WHERE u.id = ?""", USER_ROW, id).stream().findFirst();
    }

    /** Get-or-create by name, case-insensitively. The first spelling used is kept. */
    public User userByName(String name) {
        jdbc.update("INSERT INTO app_user (name, created_at) VALUES (?, ?) ON CONFLICT(name) DO NOTHING",
                    name, Instant.now().toString());
        return jdbc.query("""
                SELECT u.id, u.name, (SELECT COUNT(*) FROM display_page WHERE owner_id = u.id) AS pages
                  FROM app_user u WHERE u.name = ?""", USER_ROW, name).get(0);
    }

    // ── pages ───────────────────────────────────────────────────────────────

    /** Pages, newest update first, optionally for one owner and/or matching a name fragment. */
    public List<Page> pages(Long owner, String q) {
        StringBuilder sql = new StringBuilder(PAGE_SELECT).append(" WHERE 1=1");
        List<Object> args = new ArrayList<>();
        if (owner != null) { sql.append(" AND p.owner_id = ?"); args.add(owner); }
        if (q != null && !q.isBlank()) {
            // instr, not LIKE, so % and _ in the search text are taken literally.
            sql.append(" AND instr(lower(p.name), lower(?)) > 0");
            args.add(q.trim());
        }
        sql.append(" ORDER BY p.updated_at DESC, p.id DESC");
        return jdbc.query(sql.toString(), pageRow, args.toArray());
    }

    public Optional<Page> page(long id) {
        return jdbc.query(PAGE_SELECT + " WHERE p.id = ?", pageRow, id).stream().findFirst();
    }

    public Page createPage(long ownerId, String name, List<Integer> params) {
        String now = Instant.now().toString();
        Long id;
        try {
            id = jdbc.queryForObject("""
                    INSERT INTO display_page (owner_id, name, params, created_at, updated_at)
                    VALUES (?, ?, ?, ?, ?) RETURNING id""",
                    Long.class, ownerId, name, toJson(params), now, now);
        } catch (DataAccessException e) {
            throw clashOr(e, name);
        }
        return page(id).orElseThrow();
    }

    /** Overwrite a page's name and slots. Ownership is checked by the caller. */
    public Page updatePage(long id, String name, List<Integer> params) {
        try {
            jdbc.update("UPDATE display_page SET name = ?, params = ?, updated_at = ? WHERE id = ?",
                        name, toJson(params), Instant.now().toString(), id);
        } catch (DataAccessException e) {
            throw clashOr(e, name);
        }
        return page(id).orElseThrow();
    }

    public void deletePage(long id) {
        jdbc.update("DELETE FROM display_page WHERE id = ?", id);
    }

    // ── workspaces ──────────────────────────────────────────────────────────

    public Optional<Workspace> workspace(long userId) {
        return jdbc.query("SELECT layout, updated_at FROM user_workspace WHERE user_id = ?",
                (rs, i) -> new Workspace(rs.getString(1), rs.getString(2)), userId)
                .stream().findFirst();
    }

    public void saveWorkspace(long userId, String layout) {
        jdbc.update("""
                INSERT INTO user_workspace (user_id, layout, updated_at) VALUES (?, ?, ?)
                ON CONFLICT(user_id) DO UPDATE SET layout = excluded.layout,
                                                   updated_at = excluded.updated_at""",
                userId, layout, Instant.now().toString());
    }

    // ── row mapping ─────────────────────────────────────────────────────────

    private static final RowMapper<User> USER_ROW = (rs, i) ->
            new User(rs.getLong("id"), rs.getString("name"), rs.getInt("pages"));

    private final RowMapper<Page> pageRow = (ResultSet rs, int i) -> new Page(
            rs.getLong("id"), rs.getLong("owner_id"), rs.getString("owner_name"),
            rs.getString("name"), fromJson(rs.getString("params")),
            rs.getString("created_at"), rs.getString("updated_at"));

    private String toJson(List<Integer> params) {
        try {
            return json.writeValueAsString(params);
        } catch (JsonProcessingException e) {
            throw new IllegalStateException(e);
        }
    }

    private List<Integer> fromJson(String s) throws SQLException {
        try {
            return json.readValue(s, INT_LIST);
        } catch (JsonProcessingException e) {
            throw new SQLException("display_page.params is not a JSON array: " + s, e);
        }
    }

    /**
     * SQLite reports a UNIQUE violation as SQLITE_CONSTRAINT_UNIQUE; Spring does
     * not always translate that to DuplicateKeyException, so look at the cause.
     */
    private static RuntimeException clashOr(DataAccessException e, String name) {
        for (Throwable t = e; t != null; t = t.getCause()) {
            if (t instanceof org.sqlite.SQLiteException se
                    && se.getResultCode() == org.sqlite.SQLiteErrorCode.SQLITE_CONSTRAINT_UNIQUE) {
                return new NameClashException(name);
            }
        }
        return e;
    }
}
