import { render } from "@testing-library/react";
import { Provider } from "react-redux";
import { MemoryRouter } from "react-router";
import { ThemeProvider } from "@mui/material/styles";
import { makeStore } from "../../src/redux/store.js";
import { makeTheme } from "../../src/theme/index.js";

/** Renders `ui` with a real store (same reducers and middleware as the app), a router and the theme. */
export function renderWithProviders(ui, { preloadedState, route = "/", store = makeStore(preloadedState) } = {}) {
  const result = render(
    <Provider store={store}>
      <ThemeProvider theme={makeTheme("light")}>
        <MemoryRouter initialEntries={[route]}>{ui}</MemoryRouter>
      </ThemeProvider>
    </Provider>,
  );
  return { store, ...result };
}
