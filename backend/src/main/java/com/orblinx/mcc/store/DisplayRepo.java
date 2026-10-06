package com.orblinx.mcc.store;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.sql.ResultSet;
import java.sql.SQLException;
import java.time.Instant;
import java.util.*;

import org.springframework.dao.DataAccessException;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.jdbc.core.RowMapper;
import org.springframework.stereotype.Repository;

import com.orblinx.mcc.auth.AccountRepo;

/**
 * Display pages and each account's saved window layout, in mcc.db, owned by
 * sign-in accounts (mcc-auth.db). The two live in separate SQLite files, so
 * there is no foreign key between them: a page's owner is looked up by a
 * second query, and a deleted account's pages read as a former user's.
 *
 * Name clashes are left to the UNIQUE constraint rather than checked first, so
 * two saves at once get a clean 409 instead of a race.
 */
@Repository
public class DisplayRepo {

    /** A page always shows exactly this many parameter slots: ten rows of two. */
    public static final int SLOTS = 20;

    /** `ownerId`/`ownerName` are null once the owner's account has been deleted. */
    public record Page(long id, Long ownerId, String ownerName, String name,
                       List<Integer> params, String createdAt, String updatedAt) { }

    public record Workspace(String layout, String updatedAt) { }

    /** The owner already has a page by that name. */
    public static final class NameClashException extends RuntimeException {
        public NameClashException(String name) { super("you already have a page named '" + name + "'"); }
    }

    private static final String DDL = """
            CREATE TABLE IF NOT EXISTS display_page (
              id         INTEGER PRIMARY KEY,
              owner_id   INTEGER NOT NULL,
              name       TEXT NOT NULL,
              params     TEXT NOT NULL,
              created_at TEXT NOT NULL,
              updated_at TEXT NOT NULL,
              UNIQUE (owner_id, name)
            );
            CREATE TABLE IF NOT EXISTS account_workspace (
              account_id INTEGER PRIMARY KEY,
              layout     TEXT NOT NULL,
              updated_at TEXT NOT NULL
            );""";

    private static final String PAGE_SELECT = """
            SELECT id, owner_id, name, params, created_at, updated_at FROM display_page""";

    private static final TypeReference<List<Integer>> INT_LIST = new TypeReference<>() { };

    private final JdbcTemplate jdbc;
    private final AccountRepo accounts;
    private final ObjectMapper json = new ObjectMapper();
    /** Parameters the flown LUT downlinks. The LUT is fixed for the process, so load once. */
    private final Set<Integer> downlinked;

    /** The loader is a parameter so the decode tables are filled before the downlinked set is read. */
    public DisplayRepo(JdbcTemplate jdbc, DecodeSchemaLoader loader, AccountRepo accounts) {
        this.jdbc = jdbc;
        this.accounts = accounts;
        for (String stmt : DDL.split(";")) {
            if (!stmt.isBlank()) jdbc.execute(stmt);
        }
        this.downlinked = Set.copyOf(jdbc.queryForList(
                "SELECT DISTINCT parameter_id FROM pick_param WHERE parameter_id IS NOT NULL",
                Integer.class));
    }

    public boolean isDownlinked(int parameterId) { return downlinked.contains(parameterId); }

    // ── pages ───────────────────────────────────────────────────────────────

    /** Pages, newest update first, optionally for one owner and/or matching a name fragment. */
    public List<Page> pages(Long owner, String q) {
        StringBuilder sql = new StringBuilder(PAGE_SELECT).append(" WHERE 1=1");
        List<Object> args = new ArrayList<>();
        if (owner != null) { sql.append(" AND owner_id = ?"); args.add(owner); }
        if (q != null && !q.isBlank()) {
            // instr, not LIKE, so % and _ in the search text are taken literally.
            sql.append(" AND instr(lower(name), lower(?)) > 0");
            args.add(q.trim());
        }
        sql.append(" ORDER BY updated_at DESC, id DESC");
        return withOwnerNames(jdbc.query(sql.toString(), rawPageRow, args.toArray()));
    }

    public Optional<Page> page(long id) {
        List<Page> rows = withOwnerNames(jdbc.query(PAGE_SELECT + " WHERE id = ?", rawPageRow, id));
        return rows.stream().findFirst();
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

    public Optional<Workspace> workspace(long accountId) {
        return jdbc.query("SELECT layout, updated_at FROM account_workspace WHERE account_id = ?",
                (rs, i) -> new Workspace(rs.getString(1), rs.getString(2)), accountId)
                .stream().findFirst();
    }

    public void saveWorkspace(long accountId, String layout) {
        jdbc.update("""
                INSERT INTO account_workspace (account_id, layout, updated_at) VALUES (?, ?, ?)
                ON CONFLICT(account_id) DO UPDATE SET layout = excluded.layout,
                                                       updated_at = excluded.updated_at""",
                accountId, layout, Instant.now().toString());
    }

    // ── row mapping ─────────────────────────────────────────────────────────

    private final RowMapper<Page> rawPageRow = (ResultSet rs, int i) -> new Page(
            rs.getLong("id"), rs.getLong("owner_id"), null, rs.getString("name"),
            fromJson(rs.getString("params")), rs.getString("created_at"), rs.getString("updated_at"));

    /** Fills in each page's owner name from mcc-auth.db; "former user" once the account is gone. */
    private List<Page> withOwnerNames(List<Page> rows) {
        if (rows.isEmpty()) return rows;
        Map<Long, String> names = new HashMap<>();
        List<Page> out = new ArrayList<>(rows.size());
        for (Page p : rows) {
            String name = names.computeIfAbsent(p.ownerId(), id -> accounts.byId(id)
                    .map(a -> a.displayName() == null || a.displayName().isBlank() ? a.username() : a.displayName())
                    .orElse(null));
            out.add(name == null
                    ? new Page(p.id(), null, "former user", p.name(), p.params(), p.createdAt(), p.updatedAt())
                    : new Page(p.id(), p.ownerId(), name, p.name(), p.params(), p.createdAt(), p.updatedAt()));
        }
        return out;
    }

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
