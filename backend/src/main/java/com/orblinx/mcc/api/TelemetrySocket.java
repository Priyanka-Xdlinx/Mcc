package com.orblinx.mcc.api;

import com.orblinx.mcc.IngestService;

import java.util.Map;
import java.util.function.Consumer;
import java.util.concurrent.ConcurrentHashMap;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.socket.CloseStatus;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

/** Pushes each decoded tier block to connected browsers. */
@Component
public class TelemetrySocket extends TextWebSocketHandler {

    private static final Logger log = LoggerFactory.getLogger(TelemetrySocket.class);

    private final IngestService ingest;
    private final ObjectMapper json = new ObjectMapper();
    private final Map<WebSocketSession, Consumer<Map<String, Object>>> subs =
            new ConcurrentHashMap<>();

    public TelemetrySocket(IngestService ingest) {
        this.ingest = ingest;
    }

    @Override
    public void afterConnectionEstablished(WebSocketSession session) {
        Consumer<Map<String, Object>> l = msg -> send(session, msg);
        subs.put(session, l);
        ingest.subscribe(l);
        send(session, Map.of("type", "status", "status", ingest.status()));
        log.info("dashboard connected ({} total)", subs.size());
    }

    @Override
    public void afterConnectionClosed(WebSocketSession session, CloseStatus status) {
        Consumer<Map<String, Object>> l = subs.remove(session);
        if (l != null) ingest.unsubscribe(l);
        log.info("dashboard disconnected ({} remain)", subs.size());
    }

    private void send(WebSocketSession session, Map<String, Object> msg) {
        if (!session.isOpen()) return;
        try {
            synchronized (session) {
                session.sendMessage(new TextMessage(json.writeValueAsString(msg)));
            }
        } catch (Exception e) {
            log.debug("send failed, dropping session", e);
            try { session.close(); } catch (Exception ignored) { }
        }
    }
}
