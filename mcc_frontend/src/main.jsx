import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { Provider } from "react-redux";
// IBM Plex, bundled so the console looks the same offline (the Orblinx platform faces).
import "@fontsource/ibm-plex-sans/400.css";
import "@fontsource/ibm-plex-sans/500.css";
import "@fontsource/ibm-plex-sans/600.css";
import "@fontsource/ibm-plex-mono/400.css";
import "@fontsource/ibm-plex-mono/500.css";
import "./assets/styles/index.css";
import App from "./App.jsx";
import { bootReady } from "./redux/app/index.js";
import { restoreSession } from "./redux/auth/index.js";
import { store } from "./redux/store.js";
import "./api/authInterceptor.js";

// Render immediately; BootGate holds the routes until this settles, so a
// signed-in user never sees the sign-in page flash.
store.dispatch(restoreSession()).finally(() => store.dispatch(bootReady()));

createRoot(document.getElementById("root")).render(
  <StrictMode>
    <Provider store={store}>
      <App />
    </Provider>
  </StrictMode>,
);
