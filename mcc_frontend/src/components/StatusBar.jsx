import { useSelector } from "react-redux";
import { selectLive, selectStatus } from "../redux/telemetry/index.js";
import { crcHex } from "../utils/format.js";
import { Box, Stack, Text } from "./common/index.js";

function Fact({ k, v, dot, title }) {
  return (
    <Box title={title}>
      <Text variant="label">{k}</Text>
      <Text variant="mono" component="div">
        {dot && <span className={`dot ${dot}`} />}
        {v}
      </Text>
    </Box>
  );
}

/** The compact facts strip under the header on the working pages. */
export default function StatusBar() {
  const f = useSelector(selectStatus)?.feed;
  const live = useSelector(selectLive);
  return (
    <Stack
      direction="row"
      gap={3}
      wrap
      sx={{ px: 2, py: 1.25, bgcolor: "background.paper", borderBottom: 1, borderColor: "divider" }}
    >
      <Fact k="Feed" dot={f?.connected ? "on" : "off"} v={f ? `${f.feedHost}:${f.feedPort}` : "—"} />
      <Fact k="Master frame" v={f && f.masterFrame >= 0 ? `#${f.masterFrame}` : "—"} />
      <Fact k="Subframes" v={f?.subframesReceived ?? 0} />
      <Fact k="Spacecraft time" v={f?.spacecraftTime ?? "—"} />
      {/* Position inside a tier is the only link between the OBC and the ground,
          so the CRC of the table in flight is worth keeping on screen. */}
      <Fact k="LUT CRC-16" v={crcHex(f?.lutCrc16)} title={f ? String(f.lutCrc16) : undefined} />
      <Fact k="Lost subframes" v={f?.subframeGaps ?? 0} />
      <Fact k="Readings stored" v={(f?.historyRows ?? 0).toLocaleString()} />
      <Fact k="Dashboard stream" dot={live ? "on" : "off"} v={live ? "live" : "offline"} />
    </Stack>
  );
}
