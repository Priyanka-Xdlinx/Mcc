/** Application shell state that belongs to no one feature. */
export { default as uiReducer } from "./uiSlice.js";
export { bootReady, themeChosen, focusModeSet, THEME_KEY } from "./uiSlice.js";

export { default as notificationReducer } from "./notificationSlice.js";
export { notify, dismissNotification } from "./notificationSlice.js";

export const selectTheme = (state) => state.ui.theme;
export const selectFocusMode = (state) => state.ui.focusMode;
export const selectBootLoading = (state) => state.ui.bootLoading;
