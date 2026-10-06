import { createSlice } from "@reduxjs/toolkit";
import { readString } from "../../utils/storage.js";

/** localStorage key, also read by index.html before the first paint. */
export const THEME_KEY = "mcc.theme";

const storedTheme = () => {
  const v = readString(THEME_KEY);
  return v === "light" || v === "dark" ? v : "system";
};

/**
 * Application shell state that belongs to no one feature.
 *
 *   bootLoading  true until main.jsx's session check settles (see BootGate)
 *   theme        "system" | "light" | "dark", remembered per browser by ThemeRoot
 *   focusMode    Display page only: header hidden so the windows get the screen
 */
const uiSlice = createSlice({
  name: "ui",
  initialState: { bootLoading: true, theme: storedTheme(), focusMode: false },
  reducers: {
    bootReady(state) {
      state.bootLoading = false;
    },
    themeChosen(state, action) {
      state.theme = action.payload;
    },
    focusModeSet(state, action) {
      state.focusMode = Boolean(action.payload);
    },
  },
});

export const { bootReady, themeChosen, focusModeSet } = uiSlice.actions;
export default uiSlice.reducer;
