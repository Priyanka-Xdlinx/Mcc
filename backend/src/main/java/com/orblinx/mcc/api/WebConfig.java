package com.orblinx.mcc.api;

import org.springframework.context.annotation.Configuration;
import org.springframework.web.servlet.config.annotation.CorsRegistry;
import org.springframework.web.servlet.config.annotation.WebMvcConfigurer;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

/**
 * Wiring for the browser: the telemetry socket, plus CORS for the Vite dev
 * server. In production the built frontend is served from this same origin, so
 * the CORS entry only matters during development.
 */
@Configuration
@EnableWebSocket
public class WebConfig implements WebSocketConfigurer, WebMvcConfigurer {

    private final TelemetrySocket socket;

    public WebConfig(TelemetrySocket socket) {
        this.socket = socket;
    }

    @Override
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        registry.addHandler(socket, "/ws/telemetry").setAllowedOriginPatterns("*");
    }

    @Override
    public void addCorsMappings(CorsRegistry registry) {
        registry.addMapping("/api/**")
                .allowedOriginPatterns("http://localhost:*", "http://127.0.0.1:*")
                .allowedMethods("GET", "POST");
    }
}
