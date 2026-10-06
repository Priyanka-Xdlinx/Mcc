import { useDispatch, useSelector } from "react-redux";
import { Toast } from "./common/index.js";
import { dismissNotification } from "../redux/app/index.js";

/** The app's one toast host, connected to the notification slice. */
export default function NotificationCenter() {
  const { message, severity } = useSelector((state) => state.notification);
  const dispatch = useDispatch();
  return (
    <Toast
      open={Boolean(message)}
      message={message}
      severity={severity}
      onClose={() => dispatch(dismissNotification())}
    />
  );
}
