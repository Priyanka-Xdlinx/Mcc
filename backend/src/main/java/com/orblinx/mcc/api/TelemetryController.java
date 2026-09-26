package com.orblinx.mcc.api;

import com.orblinx.mcc.IngestService;
import com.orblinx.mcc.sim.SimulatorService;
import com.orblinx.mcc.store.TelemetryRepo;

import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/api")
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
    public List<Map<String, Object>> latest() {
        return store.latest();
    }

    /**
     * History for one parameter. Defaults to the run in progress; pass
     * {@code session=all} to reach back across earlier runs.
     */
    @GetMapping("/telemetry/{parameterId}/history")
    public List<Map<String, Object>> history(@PathVariable int parameterId,
                                             @RequestParam(defaultValue = "200") int limit,
                                             @RequestParam(required = false) String session) {
        String want = "all".equals(session) ? null
                    : (session != null ? session : String.valueOf(ingest.status().get("session")));
        return store.history(parameterId, Math.min(Math.max(limit, 1), 5000), want);
    }

    @GetMapping("/status")
    public Map<String, Object> status() {
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("feed", ingest.status());
        m.put("simulator", sim.status());
        return m;
    }
}
