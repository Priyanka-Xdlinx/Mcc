package com.orblinx.mcc.store;

import com.orblinx.mcc.decode.*;
import com.orblinx.mcc.wire.Tier;

import java.util.*;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Repository;

/**
 * Loads the decode schema and the flown lookup table out of mcc.db, once, at
 * startup. Everything here is read-only after load.
 */
@Repository
public class SchemaRepo {

    private static final Logger log = LoggerFactory.getLogger(SchemaRepo.class);

    private final JdbcTemplate jdbc;

    private final Map<Integer, Map<Long, String>> enums = new HashMap<>();
    private final Map<Integer, MessageDef> messages = new HashMap<>();
    private final List<EntryDef> entries = new ArrayList<>();
    private final Map<Integer, EntryDef> paramEntry = new HashMap<>();
    private final Map<Integer, String> paramNames = new HashMap<>();
    private int lutCrc16;

    /** The loader is a parameter so the decode tables are filled before they are read. */
    public SchemaRepo(JdbcTemplate jdbc, DecodeSchemaLoader loader) {
        this.jdbc = jdbc;
        load();
    }

    private void load() {
        jdbc.query("SELECT enum_type_id, value, label FROM enum_value", rs -> {
            enums.computeIfAbsent(rs.getInt(1), k -> new HashMap<>())
                 .put(rs.getLong(2), rs.getString(3));
        });

        Map<Integer, List<ParamDef>> byMessage = new HashMap<>();
        jdbc.query("""
                SELECT id, message_id, seq, name, offset, length, bit_offset, bit_length,
                       byte_aligned, data_type, endian, scale, unit, enum_type_id, is_array
                  FROM parameter ORDER BY message_id, seq""", rs -> {
            Integer off = (Integer) rs.getObject("offset");
            Integer bo = (Integer) rs.getObject("bit_offset");
            Integer bl = (Integer) rs.getObject("bit_length");
            Double scale = (Double) rs.getObject("scale");
            Integer enumId = (Integer) rs.getObject("enum_type_id");
            ParamDef p = new ParamDef(
                    rs.getInt("id"), rs.getInt("message_id"), rs.getInt("seq"),
                    rs.getString("name"), off, rs.getInt("length"), bo, bl,
                    rs.getBoolean("byte_aligned"), rs.getString("data_type"),
                    "big".equals(rs.getString("endian")), scale, rs.getString("unit"),
                    enumId, rs.getBoolean("is_array"));
            byMessage.computeIfAbsent(p.messageId(), k -> new ArrayList<>()).add(p);
        });

        jdbc.query("""
                SELECT m.id, s.name AS subsystem, m.name, m.direction, m.offset_unit,
                       m.length_bytes, m.node, m.msg_id
                  FROM message m JOIN subsystem s ON s.id = m.subsystem_id""", rs -> {
            int id = rs.getInt("id");
            List<ParamDef> ps = byMessage.getOrDefault(id, List.of());
            // Last definition of a repeated name wins, matching the C++ loader.
            Map<String, ParamDef> byName = new LinkedHashMap<>();
            for (ParamDef p : ps) byName.put(p.name(), p);
            messages.put(id, new MessageDef(id, rs.getString("subsystem"), rs.getString("name"),
                    rs.getString("direction"), rs.getString("offset_unit"),
                    (Integer) rs.getObject("length_bytes"), rs.getString("node"),
                    (Integer) rs.getObject("msg_id"), ps, byName));
        });

        Map<Integer, List<PickDef>> picksByEntry = new HashMap<>();
        Map<Integer, List<String>> namesByPick = new HashMap<>();
        // Block body, not an expression: List.add returns boolean, which would make
        // the lambda match both RowCallbackHandler and ResultSetExtractor.
        jdbc.query("SELECT pick_id, seq, name FROM pick_param ORDER BY pick_id, seq", rs -> {
            namesByPick.computeIfAbsent(rs.getInt(1), k -> new ArrayList<>())
                       .add(rs.getString(3));
        });
        jdbc.query("SELECT id, entry_id, seq, src, size, block FROM pick ORDER BY entry_id, seq",
                rs -> {
            int id = rs.getInt("id");
            picksByEntry.computeIfAbsent(rs.getInt("entry_id"), k -> new ArrayList<>())
                    .add(new PickDef(id, rs.getInt("seq"), rs.getInt("src"), rs.getInt("size"),
                            rs.getInt("block"), namesByPick.getOrDefault(id, List.of())));
        });

        jdbc.query("""
                SELECT e.id, e.tier, e.entry_index, e.tc_id, s.name AS subsystem, e.rsp,
                       e.apid, e.tm_raw_len, e.message_id
                  FROM lut_entry e JOIN subsystem s ON s.id = e.subsystem_id
                 ORDER BY e.tier, e.entry_index""", rs -> {
            int id = rs.getInt("id");
            entries.add(new EntryDef(id, Tier.valueOf(rs.getString("tier")),
                    rs.getInt("entry_index"), rs.getString("tc_id"), rs.getString("subsystem"),
                    rs.getString("rsp"), (Integer) rs.getObject("apid"),
                    rs.getInt("tm_raw_len"), (Integer) rs.getObject("message_id"),
                    picksByEntry.getOrDefault(id, List.of())));
        });

        // parameter id -> the LUT entry that downlinks it, for on-demand tracing.
        Map<Integer, Integer> entryOf = new HashMap<>();
        jdbc.query("""
                SELECT pp.parameter_id, k.entry_id, pp.name
                  FROM pick_param pp JOIN pick k ON k.id = pp.pick_id
                 WHERE pp.parameter_id IS NOT NULL""", rs -> {
            int pid = rs.getInt(1);
            entryOf.put(pid, rs.getInt(2));
            paramNames.put(pid, rs.getString(3));
        });
        Map<Integer, EntryDef> byId = new HashMap<>();
        for (EntryDef e : entries) byId.put(e.id(), e);
        for (Map.Entry<Integer, Integer> e : entryOf.entrySet()) {
            EntryDef def = byId.get(e.getValue());
            if (def != null) paramEntry.put(e.getKey(), def);
        }

        lutCrc16 = jdbc.queryForObject("SELECT crc16 FROM lut WHERE id = 1", Integer.class);

        int slots = entries.stream().flatMap(e -> e.picks().stream())
                           .mapToInt(p -> p.paramNames().size()).sum();
        log.info("schema loaded: {} messages, {} parameters, {} LUT entries, {} slots, CRC-16 {}",
                 messages.size(), byMessage.values().stream().mapToInt(List::size).sum(),
                 entries.size(), slots, lutCrc16);
    }

    public Map<Integer, Map<Long, String>> enums()  { return enums; }
    public List<EntryDef> entries()                 { return entries; }
    public int lutCrc16()                           { return lutCrc16; }

    public MessageDef message(Integer id) {
        return id == null ? null : messages.get(id);
    }

    /** The LUT entry that downlinks this parameter, or null if it is not downlinked. */
    public EntryDef entryForParameter(int parameterId) {
        return paramEntry.get(parameterId);
    }

    /** The parameter's name as the LUT pick spells it. */
    public String parameterName(int parameterId) {
        return paramNames.get(parameterId);
    }

    /** Entries grouped by tier, in the order the OBC lays them out. */
    public List<EntryDef> entriesOf(Tier t) {
        return entries.stream().filter(e -> e.tier() == t).toList();
    }
}
