import { useEffect } from "react";
import { useDispatch } from "react-redux";
import { telemetrySocketUrl } from "../config/env.js";
import {
  feedStatusReceived,
  loadLatest,
  refreshStatus,
  socketClosed,
  socketOpened,
  telemetryReceived,
} from "../redux/telemetry/index.js";

const STATUS_POLL_MS = 2000;
const RECONNECT_MS = 1500;

/**
 * Keeps the telemetry slice live: polls /api/status and holds the
 * /ws/telemetry socket open, reconnecting when it drops. Mounted once, by the
 * signed-in layout.
 */
export default function useTelemetryConnection() {
  const dispatch = useDispatch();

  useEffect(() => {
    dispatch(loadLatest());
    dispatch(refreshStatus());
    const t = setInterval(() => dispatch(refreshStatus()), STATUS_POLL_MS);
    return () => clearInterval(t);
  }, [dispatch]);

  useEffect(() => {
    let socket = null;
    let retry = null;
    let closed = false;

    const connect = () => {
      const s = new WebSocket(telemetrySocketUrl());
      socket = s;
      // (Re)load on every connect: if the page opened while the backend was
      // down, the table is empty and updates would have nothing to patch.
      s.onopen = () => {
        dispatch(socketOpened());
        dispatch(loadLatest());
        dispatch(refreshStatus());
      };
      s.onclose = () => {
        if (closed) return;
        dispatch(socketClosed());
        retry = setTimeout(connect, RECONNECT_MS);
      };
      s.onmessage = (ev) => {
        let msg;
        try {
          msg = JSON.parse(ev.data);
        } catch {
          return;
        }
        if (msg.type === "status" && msg.status) dispatch(feedStatusReceived(msg.status));
        else if (msg.type === "telemetry" && Array.isArray(msg.updates)) {
          dispatch(telemetryReceived({ ...msg, at: Date.now() }));
        }
      };
    };

    connect();
    return () => {
      closed = true;
      if (retry) clearTimeout(retry);
      if (socket) {
        socket.onopen = socket.onclose = socket.onmessage = null;
        socket.close();
      }
    };
  }, [dispatch]);
}
