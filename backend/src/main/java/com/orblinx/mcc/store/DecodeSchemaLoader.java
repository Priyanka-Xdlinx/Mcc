package com.orblinx.mcc.store;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.*;
import java.util.stream.Stream;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Component;

/**
 * Loads the decode schema - the mission database and the flown lookup table -
 * from the XDLINX tree into mcc.db, the first time this file has none.
 *
 * Sources: {@code xdos/mib/*.json} and {@code xdos/config/rate_group_lut_bin_gs.json}.
 * The JSON MIBs, never the flattened CSV export: the CSV has no endian column,
 * and its scale is a divisor for ADCS but a multiplier elsewhere. Here `endian`
 * is per parameter and `scale` is always a multiplier.
 *
 * An existing mcc.db is never rebuilt: its decode tables and the telemetry
 * history that refers to them stay exactly as they are. To pick up a changed
 * MIB or LUT, stop the backend and delete mcc.db (or those tables).
 *
 * Only telemetry (TM) messages are loaded: every LUT entry resolves to one, and
 * TC and EVENT definitions describe what goes up, not what comes down.
 */
@Component
public class DecodeSchemaLoader {

    private static final Logger log = LoggerFactory.getLogger(DecodeSchemaLoader.class);

    /** Tier layout of the 1280-byte subframe (src/lut/lut_format.hpp): name, offset, bytes, span, budget. */
    static final Object[][] TIERS = {
            {"VH", 0, 400, 1, 400},
            {"H", 400, 400, 2, 800},
            {"M", 800, 300, 4, 1200},
            {"L", 1100, 175, 8, 1400},
    };

    private static final String DDL = """
            CREATE TABLE IF NOT EXISTS subsystem (
                id          INTEGER PRIMARY KEY,
                name        TEXT    NOT NULL UNIQUE,
                apid        INTEGER,
                protocol    TEXT,
                source_file TEXT
            );
            CREATE TABLE IF NOT EXISTS enum_type (
                id           INTEGER PRIMARY KEY,
                subsystem_id INTEGER NOT NULL REFERENCES subsystem(id),
                name         TEXT    NOT NULL,
                UNIQUE (subsystem_id, name)
            );
            CREATE TABLE IF NOT EXISTS enum_value (
                enum_type_id INTEGER NOT NULL REFERENCES enum_type(id),
                value        INTEGER NOT NULL,
                label        TEXT,
                description  TEXT,
                PRIMARY KEY (enum_type_id, value)
            );
            CREATE TABLE IF NOT EXISTS message (
                id                 INTEGER PRIMARY KEY,
                subsystem_id       INTEGER NOT NULL REFERENCES subsystem(id),
                name               TEXT    NOT NULL,
                direction          TEXT    NOT NULL CHECK (direction IN ('TC','TM','EVENT')),
                msg_id             INTEGER,
                wire_id            INTEGER,
                node               TEXT,
                offset_unit        TEXT    NOT NULL CHECK (offset_unit IN ('bits','bytes')),
                length_bytes       INTEGER,
                is_variable_length INTEGER NOT NULL DEFAULT 0,
                UNIQUE (subsystem_id, name, direction)
            );
            CREATE TABLE IF NOT EXISTS parameter (
                id           INTEGER PRIMARY KEY,
                message_id   INTEGER NOT NULL REFERENCES message(id),
                seq          INTEGER NOT NULL,
                name         TEXT    NOT NULL,
                offset       INTEGER,
                length       INTEGER NOT NULL,
                bit_offset   INTEGER,
                bit_length   INTEGER,
                byte_aligned INTEGER NOT NULL,
                data_type    TEXT    NOT NULL,
                endian       TEXT    NOT NULL DEFAULT 'little' CHECK (endian IN ('little','big')),
                scale        REAL,
                unit         TEXT,
                enum_type_id INTEGER REFERENCES enum_type(id),
                is_array     INTEGER NOT NULL DEFAULT 0,
                array_count  INTEGER,
                description  TEXT,
                UNIQUE (message_id, seq)
            );
            CREATE INDEX IF NOT EXISTS ix_param_name ON parameter(name);
            CREATE INDEX IF NOT EXISTS ix_param_msg  ON parameter(message_id, seq);
            CREATE TABLE IF NOT EXISTS tier (
                name         TEXT    PRIMARY KEY,
                seg_offset   INTEGER NOT NULL,
                seg_bytes    INTEGER NOT NULL,
                block_span   INTEGER NOT NULL,
                block_budget INTEGER NOT NULL
            );
            CREATE TABLE IF NOT EXISTS lut (
                id             INTEGER PRIMARY KEY CHECK (id = 1),
                crc16          INTEGER NOT NULL,
                format_version INTEGER NOT NULL,
                subframe_size  INTEGER NOT NULL,
                source_file    TEXT,
                loaded_at      TEXT
            );
            CREATE TABLE IF NOT EXISTS lut_entry (
                id           INTEGER PRIMARY KEY,
                tier         TEXT    NOT NULL REFERENCES tier(name),
                entry_index  INTEGER NOT NULL,
                tc_id        TEXT    NOT NULL,
                subsystem_id INTEGER NOT NULL REFERENCES subsystem(id),
                rsp          TEXT    NOT NULL,
                apid         INTEGER,
                tm_raw_len   INTEGER NOT NULL,
                message_id   INTEGER REFERENCES message(id),
                UNIQUE (tier, entry_index)
            );
            CREATE TABLE IF NOT EXISTS pick (
                id       INTEGER PRIMARY KEY,
                entry_id INTEGER NOT NULL REFERENCES lut_entry(id),
                seq      INTEGER NOT NULL,
                src      INTEGER NOT NULL,
                size     INTEGER NOT NULL,
                block    INTEGER NOT NULL,
                UNIQUE (entry_id, seq)
            );
            CREATE TABLE IF NOT EXISTS pick_param (
                pick_id      INTEGER NOT NULL REFERENCES pick(id),
                seq          INTEGER NOT NULL,
                parameter_id INTEGER REFERENCES parameter(id),
                name         TEXT    NOT NULL,
                PRIMARY KEY (pick_id, seq)
            );
            CREATE TABLE IF NOT EXISTS parameter_limit (
                parameter_id INTEGER PRIMARY KEY REFERENCES parameter(id),
                lo           REAL    NOT NULL,
                hi           REAL    NOT NULL,
                CHECK (hi > lo)
            );""";

    private final JdbcTemplate jdbc;
    private final Path mibDir;
    private final Path lutTwin;
    private final ObjectMapper json = new ObjectMapper();

    public DecodeSchemaLoader(JdbcTemplate jdbc, @Value("${mcc.xdos-home}") String xdosHome) {
        this.jdbc = jdbc;
        Path xdos = Path.of(xdosHome).toAbsolutePath().normalize();
        this.mibDir = xdos.resolve("mib");
        this.lutTwin = xdos.resolve("config").resolve("rate_group_lut_bin_gs.json");
        for (String stmt : DDL.split(";")) {
            if (!stmt.isBlank()) jdbc.execute(stmt);
        }
        loadIfEmpty();
    }

    private void loadIfEmpty() {
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM subsystem", Long.class);
        if (n != null && n > 0) {
            log.info("decode schema already loaded ({} subsystems in mcc.db)", n);
            return;
        }
        load();
    }

    private List<Path> sources() {
        if (!Files.isDirectory(mibDir)) {
            throw new IllegalStateException("mission database not found at " + mibDir + " (set mcc.xdos-home)");
        }
        if (!Files.isRegularFile(lutTwin)) {
            throw new IllegalStateException("lookup table twin not found at " + lutTwin);
        }
        List<Path> out = new ArrayList<>();
        try (Stream<Path> s = Files.list(mibDir)) {
            s.filter(p -> p.getFileName().toString().endsWith(".json")).sorted().forEach(out::add);
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
        out.add(lutTwin);
        return out;
    }

    // ── the load itself ─────────────────────────────────────────────────────

    private void load() {
        List<Object[]> tiers = Arrays.asList(TIERS);
        jdbc.batchUpdate("INSERT INTO tier VALUES (?, ?, ?, ?, ?)", tiers);

        Map<String, Integer> apids = apidMap();
        Map<String, Integer> subId = new HashMap<>();
        Map<String, Integer> enumId = new HashMap<>();          // subsystem + '\0' + enum name
        Map<String, Integer> msgId = new HashMap<>();           // subsystem + name + direction
        Map<String, Integer> paramId = new HashMap<>();         // message id + '\0' + parameter name
        List<Object[]> subsystems = new ArrayList<>(), enumTypes = new ArrayList<>(), enumValues = new ArrayList<>(),
                messages = new ArrayList<>(), parameters = new ArrayList<>();

        for (Path path : sources()) {
            if (path.equals(lutTwin)) continue;
            JsonNode doc = read(path);
            String sub = text(doc, "subsystem");
            JsonNode msgs = doc.path("messages");
            if (sub == null || sub.isEmpty() || !msgs.isArray() || msgs.isEmpty()) continue; // ccsds_tc/tm

            int sid = subsystems.size() + 1;
            subId.put(sub, sid);
            subsystems.add(new Object[] {sid, sub, resolveApid(apids, sub), text(doc, "protocol"),
                                         path.getFileName().toString()});

            for (JsonNode en : doc.path("enums")) {
                int eid = enumTypes.size() + 1;
                enumId.put(sub + '\0' + en.path("name").asText(), eid);
                enumTypes.add(new Object[] {eid, sid, en.path("name").asText()});
                Set<Long> seen = new HashSet<>();
                for (JsonNode v : en.path("values")) {
                    Long val = longOrNull(v, "value");
                    if (val == null || !seen.add(val)) continue;          // a few MIB enums repeat a value
                    enumValues.add(new Object[] {eid, val, text(v, "name"), text(v, "description")});
                }
            }

            for (JsonNode m : msgs) {
                String direction = m.path("direction").asText();
                if (!"TM".equals(direction)) continue;
                String key = sub + '\0' + m.path("name").asText() + '\0' + direction;
                if (msgId.containsKey(key)) continue;                     // never silently shadow
                String unit = Optional.ofNullable(text(m, "offset_unit"))
                                      .or(() -> Optional.ofNullable(text(doc, "offset_unit"))).orElse("bytes");
                int mid = messages.size() + 1;
                msgId.put(key, mid);
                messages.add(new Object[] {mid, sid, m.path("name").asText(), direction, intOrNull(m, "msg_id"),
                        intOrNull(m, "wire_id"), text(m, "node"), unit, intOrNull(m, "length_bytes"),
                        m.path("is_variable_length").asBoolean(false)});

                for (JsonNode p : m.path("parameters")) {
                    Integer off = intOrNull(p, "offset");
                    Integer len = intOrNull(p, "length");
                    Integer bitOff = "bits".equals(unit) ? off : off == null ? null : off * 8;
                    Integer bitLen = "bits".equals(unit) ? len : len == null ? null : len * 8;
                    boolean aligned = bitOff != null && bitLen != null && bitOff % 8 == 0 && bitLen % 8 == 0;
                    // Prefer "enum"; the MIB also uses "enum_type" on 73 parameters.
                    String eref = Optional.ofNullable(text(p, "enum")).orElse(text(p, "enum_type"));
                    int pid = parameters.size() + 1;
                    parameters.add(new Object[] {pid, mid, p.path("seq").asInt(), p.path("name").asText(), off, len,
                            bitOff, bitLen, aligned, p.path("data_type").asText(),
                            Optional.ofNullable(text(p, "endian")).orElse("little"), doubleOrNull(p, "scale"),
                            text(p, "unit"), eref == null ? null : enumId.get(sub + '\0' + eref),
                            p.path("is_array").asBoolean(false), intOrNull(p, "array_count"), text(p, "description")});
                    paramId.put(mid + "\0" + p.path("name").asText(), pid);  // a repeated name: last wins
                }
            }
        }

        jdbc.batchUpdate("INSERT INTO subsystem (id, name, apid, protocol, source_file) VALUES (?, ?, ?, ?, ?)",
                         subsystems);
        jdbc.batchUpdate("INSERT INTO enum_type (id, subsystem_id, name) VALUES (?, ?, ?)", enumTypes);
        jdbc.batchUpdate("INSERT INTO enum_value (enum_type_id, value, label, description) VALUES (?, ?, ?, ?)",
                         enumValues);
        jdbc.batchUpdate("""
                INSERT INTO message (id, subsystem_id, name, direction, msg_id, wire_id, node, offset_unit,
                                     length_bytes, is_variable_length)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""", messages);
        jdbc.batchUpdate("""
                INSERT INTO parameter (id, message_id, seq, name, offset, length, bit_offset,
                                       bit_length, byte_aligned, data_type, endian, scale, unit, enum_type_id,
                                       is_array, array_count, description)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""", parameters);

        // ── Layer 1: the flown lookup table ─────────────────────────────────
        JsonNode twin = read(lutTwin);
        jdbc.update("INSERT INTO lut VALUES (1, ?, ?, ?, ?, ?)", twin.path("crc16").asInt(),
                    twin.path("format_version").asInt(), twin.path("subframe_size").asInt(), lutTwin.toString(),
                    Instant.now().toString());

        List<String> unresolved = new ArrayList<>();
        List<Object[]> entries = new ArrayList<>(), picks = new ArrayList<>(), slots = new ArrayList<>();
        int orphans = 0;
        for (Object[] t : TIERS) {
            String tier = (String) t[0];
            int idx = 0;
            for (JsonNode e : twin.path("tiers").path(tier)) {
                String sub = e.path("subsystem").asText();
                Integer sid = subId.get(sub);
                if (sid == null) throw new IllegalStateException("LUT entry " + text(e, "id") + " names unknown subsystem " + sub);
                Integer mid = msgId.get(sub + '\0' + e.path("rsp").asText() + '\0' + "TM");
                if (mid == null) unresolved.add(text(e, "id") + " " + sub + "/" + e.path("rsp").asText());
                int eid = entries.size() + 1;
                entries.add(new Object[] {eid, tier, idx++, e.path("id").asText(), sid, e.path("rsp").asText(),
                                          intOrNull(e, "apid"), e.path("tm_raw_len").asInt(), mid});
                int pseq = 0;
                for (JsonNode pk : e.path("picks")) {
                    int kid = picks.size() + 1;
                    picks.add(new Object[] {kid, eid, pseq++, pk.path("src").asInt(), pk.path("size").asInt(),
                                            pk.path("block").asInt()});
                    int nseq = 0;
                    for (JsonNode pname : pk.path("params")) {
                        Integer pid = mid == null ? null : paramId.get(mid + "\0" + pname.asText());
                        if (pid == null) orphans++;
                        slots.add(new Object[] {kid, nseq++, pid, pname.asText()});
                    }
                }
            }
        }
        jdbc.batchUpdate("""
                INSERT INTO lut_entry (id, tier, entry_index, tc_id, subsystem_id, rsp, apid, tm_raw_len, message_id)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)""", entries);
        jdbc.batchUpdate("INSERT INTO pick (id, entry_id, seq, src, size, block) VALUES (?, ?, ?, ?, ?, ?)", picks);
        jdbc.batchUpdate("INSERT INTO pick_param (pick_id, seq, parameter_id, name) VALUES (?, ?, ?, ?)", slots);

        // A half-resolved LUT would decode bytes into the wrong parameters and still look plausible.
        if (!unresolved.isEmpty() || orphans > 0) {
            throw new IllegalStateException("the LUT does not match the mission database: " + unresolved.size()
                    + " entries without a TM message " + unresolved + ", " + orphans + " slots without a parameter");
        }

        log.info("decode schema loaded from {}: {} subsystems, {} messages, {} parameters, {} enums, "
                 + "LUT CRC-16 {} with {} entries, {} picks, {} slots",
                 mibDir, subsystems.size(), messages.size(), parameters.size(), enumTypes.size(),
                 twin.path("crc16").asInt(), entries.size(), picks.size(), slots.size());
    }

    // ── helpers ─────────────────────────────────────────────────────────────

    /** APIDs live in the CCSDS profile, not in the subsystem MIBs. */
    private Map<String, Integer> apidMap() {
        Map<String, Integer> out = new HashMap<>();
        for (String fn : List.of("ccsds_tc.json", "ccsds_tm.json")) {
            Path p = mibDir.resolve(fn);
            if (!Files.isRegularFile(p)) continue;
            JsonNode doc = read(p);
            for (String key : List.of("apid_map", "apids", "apid")) {
                JsonNode m = doc.path(key);
                if (!m.isObject()) continue;
                m.fields().forEachRemaining(f -> {
                    if (f.getValue().isInt()) out.put(f.getKey(), f.getValue().asInt());
                });
            }
        }
        return out;
    }

    /** The profile spells XLINK-S as 'XLINK'; try the obvious aliases. */
    private static Integer resolveApid(Map<String, Integer> apids, String sub) {
        for (String key : List.of(sub, sub.replace("-S", ""), sub.toUpperCase(Locale.ROOT),
                                  sub.replace("-S", "").toUpperCase(Locale.ROOT))) {
            if (apids.containsKey(key)) return apids.get(key);
        }
        return null;
    }

    private JsonNode read(Path p) {
        try {
            return json.readTree(p.toFile());
        } catch (IOException e) {
            throw new UncheckedIOException("cannot read " + p, e);
        }
    }

    private static String text(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || v.isNull() ? null : v.asText();
    }

    private static Integer intOrNull(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || v.isNull() || !v.isNumber() ? null : v.asInt();
    }

    private static Long longOrNull(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || v.isNull() || !v.isNumber() ? null : v.asLong();
    }

    private static Double doubleOrNull(JsonNode n, String field) {
        JsonNode v = n.get(field);
        return v == null || v.isNull() || !v.isNumber() ? null : v.asDouble();
    }
}
