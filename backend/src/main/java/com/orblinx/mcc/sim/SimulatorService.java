package com.orblinx.mcc.sim;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.time.Instant;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

/**
 * Starts and stops the XDOS simulator stack on behalf of the UI.
 *
 * Only one fixed command is ever run and no argument comes from the request, so
 * the endpoint cannot be turned into a general shell.
 *
 * Stopping does NOT use the vendor's scripts/kill_all.sh: that script misses
 * xdos_tm_view and xdos_sensor_sim while printing "all XDOS processes
 * terminated", and a headless tm_view left behind waits on a signal forever.
 * The pattern below is run_stack.sh's own, which does cover them.
 */
@Service
public class SimulatorService {

    private static final Logger log = LoggerFactory.getLogger(SimulatorService.class);

    /** Matches only the simulator binaries - a looser pattern would match this JVM. */
    private static final String KILL_PATTERN =
            "build[^/]*/xdos_(eps|adcs|gps|payload|xlink|uhf|sensor)_sim"
          + "|build[^/]*/xdos_obc|build[^/]*/xdos_tm_view";

    private final Path xdosHome;
    private volatile Process process;
    private volatile Instant startedAt;
    private volatile String lastError;

    public SimulatorService(@Value("${mcc.xdos-home}") String xdosHome) {
        this.xdosHome = Path.of(xdosHome).toAbsolutePath().normalize();
    }

    public synchronized Map<String, Object> start() {
        if (isRunning()) return status();
        lastError = null;

        Path script = xdosHome.resolve("scripts/run_stack.sh");
        if (!Files.exists(script)) {
            lastError = "run_stack.sh not found at " + script;
            return status();
        }
        Path binary = xdosHome.resolve("build-mac/xdos_gs");
        if (!Files.exists(binary)) {
            lastError = "no native build at " + binary
                      + " - build it first:  scripts/build_mac.sh";
            return status();
        }

        try {
            ProcessBuilder pb = new ProcessBuilder(
                    "/bin/bash", script.toString(),
                    "--power-on", "--no-gs", "--no-windows");
            pb.directory(xdosHome.toFile());
            pb.redirectErrorStream(true);
            pb.redirectOutput(new File(xdosHome.toFile(), "logs/run_stack_ui.log"));
            process = pb.start();
            startedAt = Instant.now();
            log.info("simulator started (pid {}) in {}", process.pid(), xdosHome);
        } catch (IOException e) {
            lastError = e.getMessage();
            log.error("could not start the simulator", e);
        }
        return status();
    }

    public synchronized Map<String, Object> stop() {
        Process p = process;
        if (p != null) {
            p.descendants().forEach(ProcessHandle::destroy);
            p.destroy();
            try {
                if (!p.waitFor(3, java.util.concurrent.TimeUnit.SECONDS)) {
                    p.descendants().forEach(ProcessHandle::destroyForcibly);
                    p.destroyForcibly();
                }
            } catch (InterruptedException ie) {
                Thread.currentThread().interrupt();
            }
        }
        // run_stack.sh's own children outlive the script when it is killed
        // abruptly, so sweep them explicitly.
        runQuiet("/usr/bin/pkill", "-9", "-f", KILL_PATTERN);
        process = null;
        startedAt = null;
        log.info("simulator stopped; {} stray process(es) remain", strayProcesses().size());
        return status();
    }

    /**
     * True when the simulator stack is up, whether or not this process started it.
     *
     * The backend can be restarted while the stack keeps running - the feed is a
     * plain TCP connection and survives. Reporting "stopped" while telemetry is
     * streaming would leave the UI offering Start and refusing Stop, so ownership
     * of the Process handle is not what decides this.
     */
    public boolean isRunning() {
        Process p = process;
        if (p != null && p.isAlive()) return true;
        return !strayProcesses().isEmpty();
    }

    /** True when the stack is running but was not started by this backend. */
    public boolean isAdopted() {
        Process p = process;
        return (p == null || !p.isAlive()) && !strayProcesses().isEmpty();
    }

    /** Anything still matching the simulator pattern - used to prove a clean shutdown. */
    public List<String> strayProcesses() {
        List<String> out = new ArrayList<>();
        try {
            // Match the binaries themselves, not any command line that merely
            // mentions the path - a shell invocation containing the string would
            // otherwise report itself as a stray simulator.
            Process p = new ProcessBuilder("/usr/bin/pgrep", "-fl", KILL_PATTERN)
                    .redirectErrorStream(true).start();
            try (var r = new java.io.BufferedReader(
                    new java.io.InputStreamReader(p.getInputStream()))) {
                String line;
                while ((line = r.readLine()) != null) if (!line.isBlank()) out.add(line.trim());
            }
            p.waitFor();
        } catch (Exception ignored) { }
        return out;
    }

    public Map<String, Object> status() {
        List<String> procs = strayProcesses();        // one pgrep, not three
        Process p = process;
        boolean owned = p != null && p.isAlive();
        Map<String, Object> m = new LinkedHashMap<>();
        m.put("running", owned || !procs.isEmpty());
        m.put("adopted", !owned && !procs.isEmpty());
        m.put("pid", owned ? p.pid() : null);
        m.put("startedAt", startedAt == null ? null : startedAt.toString());
        m.put("xdosHome", xdosHome.toString());
        m.put("processes", procs);
        m.put("error", lastError);
        return m;
    }

    private void runQuiet(String... cmd) {
        try {
            new ProcessBuilder(cmd).redirectErrorStream(true).start().waitFor();
        } catch (Exception ignored) { }
    }
}
