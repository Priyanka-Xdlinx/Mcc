import { useEffect, useState } from "react";
import { useDispatch, useSelector } from "react-redux";
import simApi from "../api/simApi.js";
import { errorMessage } from "../api/apiError.js";
import { can } from "../auth/permissions.js";
import { selectUser } from "../redux/auth/index.js";
import { loadLatest, refreshStatus, selectStatus } from "../redux/telemetry/index.js";
import { Button, Icon, Stack, Text } from "./common/index.js";

/**
 * Start and stop the XDLINX telemetry stack from the browser. Stopping asks for
 * a second click: a restart costs ~30 s of OBC boot. Without SIM_CONTROL the
 * feed is shown read-only.
 */
export default function SimControl() {
  const dispatch = useDispatch();
  const sim = useSelector(selectStatus)?.simulator;
  const canControl = can(useSelector(selectUser), "SIM_CONTROL");
  const [busy, setBusy] = useState(null);
  const [confirmStop, setConfirmStop] = useState(false);
  const [failure, setFailure] = useState(null);
  const running = sim?.running ?? false;
  const error = failure ?? sim?.error ?? null;

  useEffect(() => {
    if (!confirmStop) return undefined;
    const t = setTimeout(() => setConfirmStop(false), 6000);
    return () => clearTimeout(t);
  }, [confirmStop]);

  const call = async (what) => {
    setBusy(what);
    setFailure(null);
    try {
      await simApi[what]();
    } catch (e) {
      setFailure(errorMessage(e, `${what === "start" ? "Start" : "Stop"} failed.`));
    } finally {
      setBusy(null);
      setConfirmStop(false);
      dispatch(refreshStatus());
      dispatch(loadLatest());
    }
  };

  const detail = sim
    ? running
      ? `Running · ${sim.processes.length} processes${sim.pid ? ` · pid ${sim.pid}` : ""}`
      : "Stopped"
    : "Backend not reachable";

  return (
    <Stack
      direction="row"
      gap={0.75}
      align="center"
      title={detail}
      sx={{ "& .MuiButton-root": { py: 0.25, px: 1.25, fontSize: 12.5, lineHeight: 1.6 } }}
    >
      {error && (
        <Text variant="small" truncate role="alert" title={error} sx={{ color: "error.main", maxWidth: 260 }}>
          {error}
        </Text>
      )}
      {!sim ? (
        <Button disabled>Feed control unavailable</Button>
      ) : !canControl ? (
        <Text variant="small">{running ? "Feed running" : "Feed stopped"}</Text>
      ) : running ? (
        confirmStop ? (
          <>
            <Button onClick={() => setConfirmStop(false)}>Cancel</Button>
            <Button tone="danger" variant="contained" loading={busy === "stop"} onClick={() => call("stop")}>
              <Icon name="stop" size={14} /> Confirm stop
            </Button>
          </>
        ) : (
          <Button tone="danger" disabled={busy !== null} onClick={() => setConfirmStop(true)}>
            <Icon name="stop" size={14} /> Stop feed
          </Button>
        )
      ) : (
        <Button variant="contained" loading={busy === "start"} onClick={() => call("start")}>
          <Icon name="satellite" size={15} /> Start feed
        </Button>
      )}
    </Stack>
  );
}
