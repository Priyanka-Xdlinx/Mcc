package com.orblinx.mcc.api;

import com.orblinx.mcc.sim.SimulatorService;

import io.swagger.v3.oas.annotations.Operation;
import io.swagger.v3.oas.annotations.Parameter;
import io.swagger.v3.oas.annotations.tags.Tag;

import java.util.Map;

import org.springframework.web.bind.annotation.*;

/**
 * Simulator control for the UI.
 *
 * Both endpoints take no parameters by design - the service runs one fixed
 * command, so nothing from the request reaches a shell.
 */
@RestController
@RequestMapping("/api/sim")
@Tag(name = "Feed control")
public class SimController {

    private final SimulatorService sim;

    public SimController(SimulatorService sim) {
        this.sim = sim;
    }

    @PostMapping("/start")
    @Operation(summary = "Start the XDLINX telemetry stack",
               description = "Runs one fixed command (run_stack.sh --power-on --no-gs --no-windows). "
                           + "Telemetry starts about 30 s later, once the OBC has booted.")
    public Map<String, Object> start() {
        return sim.start();
    }

    @PostMapping("/stop")
    @Operation(summary = "Stop the XDLINX telemetry stack",
               description = "Stops every stack process, including xdos_tm_view.")
    public Map<String, Object> stop() {
        return sim.stop();
    }

    @GetMapping("/status")
    @Operation(summary = "Stack status", description = "Whether the stack is running, its processes and any error.")
    public Map<String, Object> status() {
        return sim.status();
    }
}
