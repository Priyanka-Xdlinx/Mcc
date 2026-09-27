package com.orblinx.mcc.api;

import com.orblinx.mcc.IngestService;
import com.orblinx.mcc.sim.SimulatorService;
import com.orblinx.mcc.store.TelemetryRepo;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api")
@Tag(name = "Telemetry")
public class TelemetryController {

    private final TelemetryRepo store;
    private final IngestService ingest;
    private final SimulatorService sim;

    public TelemetryController(TelemetryRepo store, IngestService ingest, SimulatorService sim) {
        this.store = store;
        this.ingest = ingest;
        this.sim = sim;
    }

    /** Every parameter the flown LUT downlinks, with its latest reading if one has arrived. */
    @GetMapping("/telemetry/latest")
    @Operation(summary = "Latest value of every downlinked parameter",
               description = "One row per parameter the flown LUT downlinks (755 today), with subsystem, "
                           + "message, LUT entry, tier (VH/H/M/L), unit, the decoded value as text and as a "
                           + "number, its quality (OK, NO_ANSWER, BLOCK_INCOMPLETE, NOT_DOWNLINKED, WARMUP) "
                           + "and the master frame / subframe it came from.")
    public List<Map<String, Object>> latest() {
        return store.latest();
    }

    /**
     * History for one parameter. Defaults to the run in progress; pass
     * {@code session=all} to reach back across earlier runs.
     */
    @GetMapping("/telemetry/{parameterId}/history")
    @Operation(summary = "History of one parameter",
               description = "Stored readings, newest first. Limited to the run in progress unless "
                           + "session=all is given.")
    public List<Map<String, Object>> history(
            @Parameter(description = "Parameter id, as in /api/telemetry/latest", example = "2168")
            @PathVariable int parameterId,
            @Parameter(description = "Maximum rows, 1 to 5000") @RequestParam(defaultValue = "200") int limit,
            @Parameter(description = "Run to read; omit for the current run, 'all' for every run")
            @RequestParam(required = false) String session) {
        String want = "all".equals(session) ? null
                    : (session != null ? session : String.valueOf(ingest.status().get("session")));
        return store.history(parameterId, Math.min(Math.max(limit, 1), 5000), want);
    }

    @GetMapping("/status")
    @Operation(summary = "Feed and stack status",
               description = "Connection to the XMCC feed, master frame, spacecraft time, LUT CRC-16, "
                           + "counts of decoded values, lost subframes and incomplete blocks, plus the "
                           + "state of the XDLINX telemetry stack.")
    public Map<String, Object> status() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("feed", ingest.status());
        m.put("simulator", sim.status());
        return m;
    }
}
