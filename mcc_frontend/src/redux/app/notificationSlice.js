import { createSlice } from "@reduxjs/toolkit";

/** The one transient message on screen, shown by NotificationCenter. */
const notificationSlice = createSlice({
  name: "notification",
  initialState: { message: "", severity: "info" },
  reducers: {
    notify: {
      prepare: (message, severity = "error") => ({ payload: { message, severity } }),
      reducer(state, action) {
        state.message = action.payload.message;
        state.severity = action.payload.severity;
      },
    },
    dismissNotification(state) {
      state.message = "";
    },
  },
});

export const { notify, dismissNotification } = notificationSlice.actions;
export default notificationSlice.reducer;
