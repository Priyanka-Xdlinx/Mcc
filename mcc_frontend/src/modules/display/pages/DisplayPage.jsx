import { useEffect } from "react";
import { useSelector } from "react-redux";
import { can } from "../../../auth/permissions.js";
import { selectUser } from "../../../redux/auth/index.js";
import { selectLastFrame, selectStatus } from "../../../redux/telemetry/index.js";
import DisplayDesk from "../components/DisplayDesk.jsx";
import { setHeader } from "../utils/headerStore.js";
import "../display.css";

/** Frame header shared by every window, from the newest subframe on the socket (or the status poll). */
function useFeedHeader() {
  const status = useSelector(selectStatus);
  const lastFrame = useSelector(selectLastFrame);
  useEffect(() => {
    const f = status?.feed;
    setHeader({
      frameTime: lastFrame?.spacecraftTime ?? f?.spacecraftTime ?? null,
      frameNo: lastFrame
        ? `MF#${lastFrame.masterFrame} SF${lastFrame.subframe}`
        : f && f.masterFrame >= 0
          ? `MF#${f.masterFrame}`
          : null,
      receivedAt: lastFrame?.receivedAt ?? f?.lastReceivedAt ?? null,
      framesValid: f?.framesValid ?? f?.subframesReceived ?? null,
      framesRejected: f?.framesRejected ?? null,
      session: f?.sessionSubframes ?? null,
    });
  }, [lastFrame, status]);
}

/**
 * Display pages: floating windows of 20 parameters each. Pages are saved under
 * the signed-in user and anyone can open them; each user's window layout is
 * kept on the server. Saving needs DISPLAY_EDIT; deleting someone else's page
 * needs DISPLAY_DELETE_ANY.
 *
 * The desk is keyed by user, so a different sign-in in the same tab starts a
 * fresh desk restored from that user's own layout.
 */
export default function DisplayPage() {
  useFeedHeader();
  const user = useSelector(selectUser);
  const me = {
    id: user.id,
    canEdit: can(user, "DISPLAY_EDIT"),
    canDeleteAny: can(user, "DISPLAY_DELETE_ANY"),
  };
  return <DisplayDesk key={user.id} me={me} />;
}
