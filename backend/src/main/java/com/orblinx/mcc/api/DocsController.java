package com.orblinx.mcc.api;

import com.orblinx.mcc.MccApplication;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.tags.Tag;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

/**
 * The database schema, read from the live SQLite catalogues rather than
 * written by hand, so the Docs page can never describe a table that no longer
 * exists. Two files: mcc.db (decode, telemetry, display) and mcc-auth.db
 * (accounts), shown as one list.
 */
@RestController
@RequestMapping("/api")
@Tag(name = "Documentation")
public class DocsController {

    private final JdbcTemplate db;
    private final JdbcTemplate authDb;

    public DocsController(JdbcTemplate jdbc, @Value("${mcc.auth.db}") String authDbPath) {
        this.db = jdbc;
        this.authDb = new JdbcTemplate(MccApplication.sqliteDataSource(authDbPath));
    }

    @GetMapping("/schema")
    @Operation(summary = "Database schema",
               description = "Every table in mcc.db and mcc-auth.db with its columns, primary key, "
                           + "foreign keys, indexes and current row count, read from the SQLite catalogues.")
    public Map<String, Object> schema() {
        List<Map<String, Object>> tables = new ArrayList<>();
        for (Map<String, Object> t : tablesOf(db)) tables.add(t);
        for (Map<String, Object> t : tablesOf(authDb)) tables.add(t);

        Map<String, Object> out = new LinkedHashMap<>();
        out.put("engine", "SQLite " + db.queryForObject("SELECT sqlite_version()", String.class));
        out.put("database", "mcc.db + mcc-auth.db");
        long size = db.queryForObject("SELECT page_size * page_count FROM pragma_page_size, pragma_page_count",
                                      Long.class)
                  + authDb.queryForObject("SELECT page_size * page_count FROM pragma_page_size, pragma_page_count",
                                          Long.class);
        out.put("sizeBytes", size);
        out.put("tables", tables);
        return out;
    }

    private static List<Map<String, Object>> tablesOf(JdbcTemplate jdbc) {
        List<Map<String, Object>> tables = new ArrayList<>();
        List<String> names = jdbc.queryForList(
                "SELECT name FROM sqlite_master WHERE type = 'table' AND name NOT LIKE 'sqlite_%' ORDER BY rowid",
                String.class);
        for (String t : names) tables.add(table(jdbc, t));
        return tables;
    }

    /** One table; `t` comes from the catalogue above, never from a request. */
    private static Map<String, Object> table(JdbcTemplate jdbc, String t) {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("name", t);
        m.put("rows", jdbc.queryForObject("SELECT COUNT(*) FROM \"" + t + "\"", Long.class));
        m.put("columns", jdbc.query("PRAGMA table_info(\"" + t + "\")", (rs, i) -> {
            Map<String, Object> c = new LinkedHashMap<>();
            c.put("name", rs.getString("name"));
            c.put("type", rs.getString("type"));
            c.put("notNull", rs.getInt("notnull") == 1);
            c.put("pk", rs.getInt("pk"));
            c.put("default", rs.getString("dflt_value"));
            return c;
        }));
        m.put("foreignKeys", jdbc.query("PRAGMA foreign_key_list(\"" + t + "\")", (rs, i) -> {
            Map<String, Object> f = new LinkedHashMap<>();
            f.put("column", rs.getString("from"));
            f.put("references", rs.getString("table"));
            f.put("referencesColumn", rs.getString("to"));
            return f;
        }));
        List<Map<String, Object>> idx = new ArrayList<>();
        for (Map<String, Object> ix : jdbc.queryForList("PRAGMA index_list(\"" + t + "\")")) {
            String ixName = String.valueOf(ix.get("name"));
            Map<String, Object> x = new LinkedHashMap<>();
            x.put("name", ixName);
            x.put("unique", Integer.valueOf(1).equals(ix.get("unique")));
            x.put("origin", ix.get("origin"));   // c = CREATE INDEX, u = UNIQUE, pk = primary key
            x.put("columns", jdbc.queryForList(
                    "SELECT name FROM pragma_index_info(?) ORDER BY seqno", String.class, ixName));
            idx.add(x);
        }
        m.put("indexes", idx);
        return m;
    }
}
