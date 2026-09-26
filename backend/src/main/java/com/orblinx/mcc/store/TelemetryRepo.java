package com.orblinx.mcc.store;

import com.orblinx.mcc.decode.DecodedValue;

import java.time.Instant;
import java.util.List;
import java.util.Map;
import javax.sql.DataSource;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.jdbc.core.BatchPreparedStatementSetter;
import org.springframework.jdbc.core.JdbcTemplate;
import org.springframework.stereotype.Repository;

/**
 * Persists decoded telemetry: a latest-value row per parameter for the live
 * dashboard, plus an append-only history for replay and charting.
 */
@Repository
public class TelemetryRepo {

    private final JdbcTemplate jdbc;
    private final boolean historyEnabled;
    private final long historyMaxRows;

    public TelemetryRepo(DataSource ds,
                         @Value("${mcc.history.enabled:true}") boolean historyEnabled,
                         @Value("${mcc.history.max-rows:2000000}") long historyMaxRows) {
        this.jdbc = new JdbcTemplate(ds);
        this.historyEnabled = historyEnabled;
        this.historyMaxRows = historyMaxRows;
    }

    private static final String UPSERT_LATEST = """
            INSERT INTO telemetry_latest (parameter_id, entry_id, master_frame, subframe,
                                          spacecraft_time, received_at, raw_hex,
                                          value_num, value_text, quality)
            VALUES (?,?,?,?,?,?,?,?,?,?)
            ON CONFLICT(parameter_id) DO UPDATE SET
                entry_id        = excluded.entry_id,
                master_frame    = excluded.master_frame,
                subframe        = excluded.subframe,
                spacecraft_time = excluded.spacecraft_time,
                received_at     = excluded.received_at,
                raw_hex         = excluded.raw_hex,
                value_num       = excluded.value_num,
                value_text      = excluded.value_text,
                quality         = excluded.quality""";

    private static final String INSERT_HISTORY = """
            INSERT INTO telemetry_history (session, parameter_id, master_frame, subframe,
                                           spacecraft_time, received_at,
                                           value_num, value_text, quality)
            VALUES (?,?,?,?,?,?,?,?,?)""";

    /** Store one entry's worth of readings. Values with no parameter id are skipped. */
    public void store(String session, int entryId, long masterFrame, int subframe,
                      String spacecraftTime, List<DecodedValue> values) {
        List<DecodedValue> rows = values.stream().filter(v -> v.parameterId() != null).toList();
        if (rows.isEmpty()) return;
        String now = Instant.now().toString();

        jdbc.batchUpdate(UPSERT_LATEST, new BatchPreparedStatementSetter() {
            public void setValues(java.sql.PreparedStatement ps, int i) throws java.sql.SQLException {
                DecodedValue v = rows.get(i);
                ps.setInt(1, v.parameterId());
                ps.setInt(2, entryId);
                ps.setLong(3, masterFrame);
                ps.setInt(4, subframe);
                ps.setString(5, spacecraftTime);
                ps.setString(6, now);
                ps.setString(7, null);
                if (v.valueNum() == null) ps.setNull(8, java.sql.Types.REAL);
                else ps.setDouble(8, v.valueNum());
                ps.setString(9, v.formatted());
                ps.setString(10, v.quality().name());
            }
            public int getBatchSize() { return rows.size(); }
        });

        if (!historyEnabled) return;
        jdbc.batchUpdate(INSERT_HISTORY, new BatchPreparedStatementSetter() {
            public void setValues(java.sql.PreparedStatement ps, int i) throws java.sql.SQLException {
                DecodedValue v = rows.get(i);
                ps.setString(1, session);
                ps.setInt(2, v.parameterId());
                ps.setLong(3, masterFrame);
                ps.setInt(4, subframe);
                ps.setString(5, spacecraftTime);
                ps.setString(6, now);
                if (v.valueNum() == null) ps.setNull(7, java.sql.Types.REAL);
                else ps.setDouble(7, v.valueNum());
                ps.setString(8, v.formatted());
                ps.setString(9, v.quality().name());
            }
            public int getBatchSize() { return rows.size(); }
        });
    }

    /** Keep history bounded; called occasionally rather than per subframe. */
    public void pruneHistory() {
        if (!historyEnabled) return;
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM telemetry_history", Long.class);
        if (n != null && n > historyMaxRows) {
            jdbc.update("""
                    DELETE FROM telemetry_history WHERE id IN (
                        SELECT id FROM telemetry_history ORDER BY id ASC LIMIT ?)""",
                    n - historyMaxRows);
        }
    }

    /** Snapshot for the dashboard: every parameter the LUT currently downlinks. */
    public List<Map<String, Object>> latest() {
        return jdbc.queryForList("""
                SELECT p.id            AS parameter_id,
                       s.name          AS subsystem,
                       m.name          AS message,
                       e.tc_id         AS entry,
                       e.tier          AS tier,
                       p.name          AS name,
                       p.unit          AS unit,
                       p.data_type     AS data_type,
                       p.byte_aligned  AS byte_aligned,
                       t.value_text    AS value,
                       t.value_num     AS value_num,
                       t.quality       AS quality,
                       t.master_frame  AS master_frame,
                       t.subframe      AS subframe,
                       t.spacecraft_time AS spacecraft_time,
                       t.received_at   AS received_at
                  FROM pick_param pp
                  JOIN pick   k ON k.id = pp.pick_id
                  JOIN lut_entry e ON e.id = k.entry_id
                  JOIN parameter p ON p.id = pp.parameter_id
                  JOIN message   m ON m.id = p.message_id
                  JOIN subsystem s ON s.id = m.subsystem_id
                  LEFT JOIN telemetry_latest t ON t.parameter_id = p.id
                 ORDER BY e.tier, e.entry_index, k.seq, pp.seq""");
    }

    /** History for one parameter, restricted to a single run. */
    public List<Map<String, Object>> history(int parameterId, int limit, String session) {
        if (session == null || session.isBlank()) {
            return jdbc.queryForList("""
                    SELECT id, session, master_frame, subframe, spacecraft_time, received_at,
                           value_num, value_text, quality
                      FROM telemetry_history
                     WHERE parameter_id = ?
                     ORDER BY id DESC LIMIT ?""", parameterId, limit);
        }
        return jdbc.queryForList("""
                SELECT id, session, master_frame, subframe, spacecraft_time, received_at,
                       value_num, value_text, quality
                  FROM telemetry_history
                 WHERE parameter_id = ? AND session = ?
                 ORDER BY id DESC LIMIT ?""", parameterId, session, limit);
    }

    public long historyRows() {
        Long n = jdbc.queryForObject("SELECT COUNT(*) FROM telemetry_history", Long.class);
        return n == null ? 0 : n;
    }
}
