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
          + "|build[^/]*/xdos_obc|build[^/]*/xdos_tm_view"
          + "|feed/xmcc_replay\\.py";

    private static final boolean WINDOWS = System.getProperty("os.name", "").toLowerCase().contains("win");

    private final Path xdosHome;
    private volatile Process process;
    private volatile Instant startedAt;
    private volatile String lastError;
    private volatile String mode;                  // "native" or "replay"

    public SimulatorService(@Value("${mcc.xdos-home}") String xdosHome) {
        this.xdosHome = Path.of(xdosHome).toAbsolutePath().normalize();
    }

    /**
     * Starts a telemetry feed on 127.0.0.1:9100: the native XDLINX test stack when it has been
     * built, otherwise the recorded feed (feed/xmcc_replay.py, Python standard library only), so
     * a fresh clone runs without a C++ build. MCC_FEED=replay forces the recorded feed.
     */
    public synchronized Map<String, Object> start() {
        if (isRunning()) return status();
        lastError = null;
        boolean forceReplay = "replay".equalsIgnoreCase(System.getenv("MCC_FEED"));
        String buildDir = System.getenv().getOrDefault("XDOS_BUILD",
                System.getProperty("os.name", "").toLowerCase().contains("mac") ? "build-mac" : "build");
        // The native stack is started by a bash script, so on Windows only the recording runs.
        boolean nativeBuilt = !WINDOWS && Files.exists(xdosHome.resolve(buildDir).resolve("xdos_gs"));
        if (forceReplay || !nativeBuilt) return startReplay(nativeBuilt ? null : buildDir);
        return startNative();
    }

    private Map<String, Object> startReplay(String missingBuildDir) {
        Path root = xdosHome.getParent();          // the mcc-app repository
        Path script = root.resolve("feed/xmcc_replay.py");
        Path capture = root.resolve("testdata/feed_capture.xmcc");
        String python = findPython();
        if (!Files.exists(script) || !Files.exists(capture) || python == null) {
            lastError = "no feed available: " + (python == null ? "python3 not found" : "recorded feed missing at " + capture)
                      + (missingBuildDir == null ? "" : "; and no native build at " + xdosHome.resolve(missingBuildDir)
                         + " (build it with xdos/scripts/build_mac.sh on macOS, CMake on Linux)");
            return status();
        }
        try {
            Files.createDirectories(xdosHome.resolve("logs"));
            ProcessBuilder pb = new ProcessBuilder(python, script.toString(), "--capture", capture.toString());
            pb.directory(root.toFile());
            pb.redirectErrorStream(true);
            pb.redirectOutput(new File(xdosHome.toFile(), "logs/xmcc_replay.log"));
            process = pb.start();
            startedAt = Instant.now();
            mode = "replay";
            Files.writeString(pidFile(), Long.toString(process.pid()));
            log.info("recorded feed started (pid {}): {} via {}{}", process.pid(), capture, python,
                     missingBuildDir == null ? "" : " - no native test-stack build, so using the recording");
        } catch (IOException e) {
            lastError = e.getMessage();
            log.error("could not start the recorded feed", e);
        }
        return status();
    }

    /** Pid of the recorded feed, so it can be found and stopped after a backend restart on any OS. */
    private Path pidFile() { return xdosHome.getParent().resolve(".feed.pid"); }

    private java.util.Optional<ProcessHandle> replayFromPidFile() {
        try {
            long pid = Long.parseLong(Files.readString(pidFile()).trim());
            return ProcessHandle.of(pid).filter(ProcessHandle::isAlive);
        } catch (Exception e) {
            return java.util.Optional.empty();
        }
    }

    /** First python3 that runs: Homebrew, /usr/local, then PATH. */
    private static String findPython() {
        String[] cands = WINDOWS ? new String[] {"py", "python", "python3"}
                                 : new String[] {"/opt/homebrew/bin/python3", "/usr/local/bin/python3", "/usr/bin/python3", "python3"};
        for (String cand : cands) {
            try {
                Process p = new ProcessBuilder(cand, "-c", "pass").redirectErrorStream(true).start();
                if (p.waitFor(5, java.util.concurrent.TimeUnit.SECONDS) && p.exitValue() == 0) return cand;
            } catch (Exception ignored) { }
        }
        return null;
    }

    private Map<String, Object> startNative() {
        Path script = xdosHome.resolve("scripts/run_stack.sh");
        if (!Files.exists(script)) {
            lastError = "run_stack.sh not found at " + script;
            return status();
        }
        try {
            ProcessBuilder pb = new ProcessBuilder(
                    "/bin/bash", script.toString(),
                    "--power-on", "--no-gs", "--no-windows");
            pb.directory(xdosHome.toFile());
            pb.redirectErrorStream(true);
            // logs/ is git-ignored, so a fresh clone does not have it yet.
            Files.createDirectories(xdosHome.resolve("logs"));
            // The OBC stays silent until a downlink map (LUT) has been uplinked once; it then
            // reloads it from obc_lut_store.bin on every boot. That file is runtime state and
            // git-ignored, so on a fresh clone preload it with the flown LUT - byte-for-byte what
            // an uplink from xdos_gs would store.
            Path store = xdosHome.resolve("obc_lut_store.bin");
            Path flownLut = xdosHome.resolve("config/rate_group_lut.bin");
            if (!Files.exists(store) && Files.exists(flownLut)) {
                Files.copy(flownLut, store);
                log.info("no uplinked LUT yet - preloaded {} as {}", flownLut, store);
            }
            pb.redirectOutput(new File(xdosHome.toFile(), "logs/run_stack_ui.log"));
            process = pb.start();
            startedAt = Instant.now();
            mode = "native";
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
        replayFromPidFile().ifPresent(ProcessHandle::destroyForcibly);
        try { Files.deleteIfExists(pidFile()); } catch (IOException ignored) { }
        // run_stack.sh's own children outlive the script when it is killed
        // abruptly, so sweep them explicitly (no pkill on Windows; nothing native runs there).
        if (!WINDOWS) runQuiet("/usr/bin/pkill", "-9", "-f", KILL_PATTERN);
        process = null;
        startedAt = null;
        mode = null;
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
        return replayFromPidFile().isPresent() || !strayProcesses().isEmpty();
    }

    /** True when the stack is running but was not started by this backend. */
    public boolean isAdopted() {
        Process p = process;
        return (p == null || !p.isAlive()) && (replayFromPidFile().isPresent() || !strayProcesses().isEmpty());
    }

    /** Anything still matching the simulator pattern - used to prove a clean shutdown. */
    public List<String> strayProcesses() {
        List<String> out = new ArrayList<>();
        replayFromPidFile().ifPresent(h -> out.add(h.pid() + " feed/xmcc_replay.py"));
        if (WINDOWS) return out;
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
        m.put("mode", mode != null ? mode
                : procs.stream().anyMatch(l -> l.contains("xmcc_replay")) ? "replay"
                : procs.isEmpty() ? null : "native");
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
