import { useDispatch, useSelector } from "react-redux";
import { useLocation, useNavigate } from "react-router";
import logoInk from "../assets/images/logo-ink-800.png";
import logoWhite from "../assets/images/logo-white-800.png";
import { pageOf, pagesFor } from "../config/navigation.js";
import useNow from "../hooks/useNow.js";
import useResolvedTheme from "../hooks/useResolvedTheme.js";
import { selectUser } from "../redux/auth/index.js";
import { focusModeSet, selectFocusMode, themeChosen } from "../redux/app/index.js";
import { selectLastMsgAt, selectLive, selectStatus } from "../redux/telemetry/index.js";
import { FRESH_MS } from "../utils/format.js";
import SimControl from "./SimControl.jsx";
import UserMenu from "./UserMenu.jsx";
import { Box, Icon, IconButton, NavTabs, Stack, Text } from "./common/index.js";

/** "Receiving" only while messages arrive; "No data" while connected but silent. */
function LinkPill() {
  const status = useSelector(selectStatus);
  const live = useSelector(selectLive);
  const lastMsgAt = useSelector(selectLastMsgAt);
  const now = useNow(1000);
  const connected = Boolean(status?.feed.connected) && live;
  const receiving = connected && lastMsgAt != null && now - lastMsgAt < FRESH_MS;
  const state = receiving ? "on" : connected ? "idle" : "off";
  return (
    <span className={`linkpill ${state}`} role="status">
      <span className={`dot ${state}`} />
      {receiving ? "Receiving" : connected ? "No data" : status ? "Feed stopped" : "Offline"}
    </span>
  );
}

/** One line: brand, page tabs, then feed status and controls. Always shown. */
export default function AppHeader() {
  const dispatch = useDispatch();
  const navigate = useNavigate();
  const { pathname } = useLocation();
  const resolved = useResolvedTheme();
  const onDisplay = pageOf(pathname)?.path === "/display";
  const focused = useSelector(selectFocusMode);
  const user = useSelector(selectUser);
  // Only the pages this user may open.
  const nav = pagesFor(user).map((p) => ({ value: p.path, label: p.label, hint: p.hint, icon: <Icon name={p.icon} size={14} /> }));

  return (
    <Box component="header" sx={{ bgcolor: "background.paper", borderBottom: 1, borderColor: "divider" }}>
      <Stack direction="row" align="center" gap={1} sx={{ px: 1.5, minHeight: 40 }}>
        <Stack direction="row" align="center" gap={1} sx={{ flex: "none" }} title="Mission control · XMCC telemetry">
          <img src={resolved === "dark" ? logoWhite : logoInk} alt="XDLINX Space Labs" height={18} />
          <Box sx={{ width: "1px", height: 18, bgcolor: "divider" }} />
          <Text variant="title" sx={{ whiteSpace: "nowrap", fontSize: 14 }}>
            OrbLinx MCC
          </Text>
        </Stack>
        {onDisplay && (
          <IconButton
            label={focused ? "Show the display toolbar" : "Hide the display toolbar to focus on the windows"}
            onClick={() => dispatch(focusModeSet(!focused))}
          >
            <Icon name={focused ? "down" : "up"} size={16} />
          </IconButton>
        )}
        {/* Takes the space left over and scrolls its tabs when that runs out. */}
        <Box sx={{ flex: 1, minWidth: 0 }}>
          <NavTabs label="Pages" value={pageOf(pathname)?.path} items={nav} onChange={(path) => navigate(path)} />
        </Box>
        <Stack direction="row" align="center" gap={0.75} sx={{ flex: "none" }}>
          <LinkPill />
          {/* Follows the OS until first pressed; then the choice is remembered. */}
          <IconButton
            label={resolved === "dark" ? "Switch to light mode" : "Switch to dark mode"}
            onClick={() => dispatch(themeChosen(resolved === "dark" ? "light" : "dark"))}
          >
            <Icon name={resolved === "dark" ? "sun" : "moon"} size={16} />
          </IconButton>
          <SimControl />
          <UserMenu />
        </Stack>
      </Stack>
    </Box>
  );
}
