package com.orblinx.mcc.api;

import com.orblinx.mcc.sim.SimulatorService;

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
public class SimController {

    private final SimulatorService sim;

    public SimController(SimulatorService sim) {
        this.sim = sim;
    }

    @PostMapping("/start")
    public Map<String, Object> start() {
        return sim.start();
    }

    @PostMapping("/stop")
    public Map<String, Object> stop() {
        return sim.stop();
    }

    @GetMapping("/status")
    public Map<String, Object> status() {
        return sim.status();
    }
}
