package com.orblinx.mcc;

import com.orblinx.mcc.decode.*;
import com.orblinx.mcc.store.SchemaRepo;
import com.orblinx.mcc.store.TelemetryRepo;
import com.orblinx.mcc.wire.*;

import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.*;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.Consumer;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

/**
 * Composition root for the decode chain:
 *
 *   port 9100 -> XmccClient -> TierAssembler -> ParameterDecoder -> SQLite + WebSocket
 *
 * Two things here are load-bearing and easy to leave out:
 *
 *   - The first master frame after OBC boot is a warm-up. The OBC starts with
 *     every pick range set to 0xFF and polls each entry only when its turn comes
 *     round, so VH is valid from subframe 1 but L does not fill at all within
 *     that frame. Those readings are marked WARMUP, not published as values.
 *   - The twin's CRC-16 must match the table the OBC is flying. Position inside
 *     a tier is the only link between the two sides, so a mismatched twin reads
 *     every value from the wrong offset and still looks entirely plausible.
 */
@Service
public class IngestService {

    private static final Logger log = LoggerFactory.getLogger(IngestService.class);

    private final SchemaRepo schema;
    private final TelemetryRepo store;
    private final ParameterDecoder decoder;
    private final TierAssembler assembler = new TierAssembler();
    /** Twenty master frames of history, so a past frame can be selected and decoded again. */
    private final FrameBuffer frames = new FrameBuffer(320);

    private final String host;
    private final int port;
    private final long reconnectDelayMs;
    private final boolean enforceCrc;
    private final Path twinPath;

    private XmccClient client;
    private final List<Consumer<Map<String, Object>>> listeners = new ArrayList<>();

    private volatile long warmupMasterFrame = -1;
    private volatile long currentMasterFrame = -1;
    private volatile String lastSpacecraftTime;
    private final AtomicLong decodedValues = new AtomicLong();
    private final AtomicLong incompleteBlocks = new AtomicLong();
    private final AtomicLong subframeGaps = new AtomicLong();
    private volatile int lastSubframe = -1;
    /**
     * Identifies one run of the spacecraft. The master-frame counter restarts at
     * 0 every time the OBC boots, so stored history has to be separable by run or
     * two passes plot as one impossible timeline.
     */
    private volatile String session = newSession();

    private static String newSession() {
        return java.time.Instant.now().toString();
    }

    public IngestService(SchemaRepo schema, TelemetryRepo store,
                         @Value("${mcc.feed.host}") String host,
                         @Value("${mcc.feed.port}") int port,
                         @Value("${mcc.feed.reconnect-delay-ms:1000}") long reconnectDelayMs,
                         @Value("${mcc.feed.enforce-crc:true}") boolean enforceCrc,
                         @Value("${mcc.xdos-home}") String xdosHome) {
        this.schema = schema;
        this.store = store;
        this.decoder = new ParameterDecoder(schema.enums());
        this.host = host;
        this.port = port;
        this.reconnectDelayMs = reconnectDelayMs;
        this.enforceCrc = enforceCrc;
        this.twinPath = Path.of(xdosHome, "config", "rate_group_lut_bin_gs.json");
    }

    @PostConstruct
    void start() {
        verifyLut();
        client = new XmccClient(host, port, reconnectDelayMs, this::onSubframe);
        client.start();
    }

    @PreDestroy
    void stop() {
        if (client != null) client.stop();
    }

    /**
     * Cross-check the schema's LUT against the twin file on disk. They diverge
     * when the ground station's table is re-uplinked but the schema is not
     * rebuilt - at which point every decoded value is read from the wrong offset.
     */
    private void verifyLut() {
        if (!enforceCrc) {
            log.warn("LUT CRC enforcement disabled - decoded values are not trustworthy");
            return;
        }
        try {
            JsonNode twin = new ObjectMapper().readTree(Files.readString(twinPath));
            int onDisk = twin.get("crc16").asInt();
            if (onDisk != schema.lutCrc16()) {
                throw new IllegalStateException(String.format(
                        "LUT mismatch: schema was built from CRC-16 %d but %s now has %d. "
                        + "Rebuild the schema (schema/build_schema.py) before decoding.",
                        schema.lutCrc16(), twinPath, onDisk));
            }
            log.info("LUT verified: CRC-16 {} matches {}", onDisk, twinPath);
        } catch (IllegalStateException e) {
            throw e;
        } catch (Exception e) {
            throw new IllegalStateException("cannot read the LUT twin at " + twinPath
                    + " - decoding refused: " + e.getMessage(), e);
        }
    }

    // ── per-subframe pipeline ───────────────────────────────────────────────

    void onSubframe(Subframe sf) {
        frames.add(sf);
        if (warmupMasterFrame < 0) {
            warmupMasterFrame = sf.masterFrame();
            log.info("first master frame is #{} - treating it as warm-up", warmupMasterFrame);
        }
        if (currentMasterFrame >= 0 && sf.masterFrame() < currentMasterFrame) {
            // The counter went backwards: the OBC rebooted, so this is a new run.
            session = newSession();
            warmupMasterFrame = sf.masterFrame();
            log.info("master frame went {} -> {}: new run, session {}",
                     currentMasterFrame, sf.masterFrame(), session);
        }
        if (sf.masterFrame() != currentMasterFrame) {
            for (TierAssembler.Pending p : assembler.drainBefore(sf.masterFrame())) {
                incompleteBlocks.incrementAndGet();
                log.debug("tier {} block {}/{} abandoned with {}/{} slices",
                          p.tier(), p.masterFrame(), p.blockIndex(),
                          p.slicesHave(), p.tier().blockSpan);
                publishIncomplete(p);
            }
            currentMasterFrame = sf.masterFrame();
            lastSubframe = -1;
        }
        if (lastSubframe >= 0 && sf.subframe() != lastSubframe + 1) {
            // Numbering never shifts to hide a loss, so a jump is a real gap.
            subframeGaps.incrementAndGet();
            log.warn("subframe gap: {} -> {} in master frame {}",
                     lastSubframe, sf.subframe(), sf.masterFrame());
        }
        lastSubframe = sf.subframe();
        lastSpacecraftTime = sf.spacecraftTime();

        boolean warmup = sf.masterFrame() == warmupMasterFrame;

        for (Tier tier : Tier.values()) {
            byte[] block = assembler.feed(tier, sf);
            if (block == null) continue;
            decodeTier(tier, block, sf, warmup ? Quality.WARMUP : Quality.OK);
        }
    }

    private void decodeTier(Tier tier, byte[] block, Subframe sf, Quality blockQuality) {
        List<Map<String, Object>> updates = new ArrayList<>();
        for (EntryDef e : schema.entriesOf(tier)) {
            MessageDef msg = schema.message(e.messageId());
            List<DecodedValue> values = decoder.decodeEntry(e, msg, block, blockQuality);
            if (values.isEmpty()) continue;
            store.store(session, e.id(), sf.masterFrame(), sf.subframe(),
                        sf.spacecraftTime(), values);
            decodedValues.addAndGet(values.size());
            for (DecodedValue v : values) {
                if (v.parameterId() == null) continue;
                updates.add(Map.of(
                        "parameterId", v.parameterId(),
                        "entry", e.tcId(),
                        "tier", tier.name(),
                        "name", v.name(),
                        "value", v.formatted(),
                        "quality", v.quality().name()));
            }
        }
        if (!updates.isEmpty()) {
            broadcast(Map.of(
                    "type", "telemetry",
                    "tier", tier.name(),
                    "masterFrame", sf.masterFrame(),
                    "subframe", sf.subframe(),
                    "spacecraftTime", sf.spacecraftTime(),
                    "updates", updates));
        }
    }

    private void publishIncomplete(TierAssembler.Pending p) {
        broadcast(Map.of(
                "type", "blockIncomplete",
                "tier", p.tier().name(),
                "masterFrame", p.masterFrame(),
                "blockIndex", p.blockIndex(),
                "slicesReceived", p.slicesHave(),
                "slicesExpected", p.tier().blockSpan));
    }

    // ── subscriptions and status ────────────────────────────────────────────

    public synchronized void subscribe(Consumer<Map<String, Object>> l) { listeners.add(l); }
    public synchronized void unsubscribe(Consumer<Map<String, Object>> l) { listeners.remove(l); }

    private void broadcast(Map<String, Object> msg) {
        List<Consumer<Map<String, Object>>> copy;
        synchronized (this) { copy = new ArrayList<>(listeners); }
        for (Consumer<Map<String, Object>> l : copy) {
            try { l.accept(msg); } catch (Exception e) { log.debug("listener failed", e); }
        }
    }

    public FrameBuffer frames() { return frames; }

    /**
     * Keep the history table bounded.
     *
     * Every tier writes when its block completes, so VH alone stores 148 rows a
     * second and the four tiers together about 350 - roughly 230 MB an hour.
     * Without this the database grows until the disk does, which on a long run
     * is a failure that arrives quietly.
     */
    @org.springframework.scheduling.annotation.Scheduled(fixedDelay = 30_000)
    void prune() {
        try {
            store.pruneHistory();
        } catch (Exception e) {
            log.warn("history prune failed: {}", e.getMessage());
        }
    }

    public Map<String, Object> status() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("feedHost", host);
        m.put("feedPort", port);
        m.put("connected", client != null && client.isConnected());
        m.put("subframesReceived", client == null ? 0 : client.subframesReceived());
        m.put("masterFrame", currentMasterFrame);
        m.put("warmupMasterFrame", warmupMasterFrame);
        m.put("spacecraftTime", lastSpacecraftTime);
        m.put("lutCrc16", schema.lutCrc16());
        m.put("lutEntries", schema.entries().size());
        m.put("decodedValues", decodedValues.get());
        m.put("incompleteBlocks", incompleteBlocks.get());
        m.put("subframeGaps", subframeGaps.get());
        m.put("historyRows", store.historyRows());
        m.put("bufferedFrames", frames.size());
        m.put("session", session);
        return m;
    }
}
