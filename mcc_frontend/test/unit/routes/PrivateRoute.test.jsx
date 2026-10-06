import { screen } from "@testing-library/react";
import { Route, Routes } from "react-router";
import PrivateRoute from "../../../src/routes/PrivateRoute.jsx";
import { makeStore } from "../../../src/redux/store.js";
import { renderWithProviders } from "../../utils/renderWithProviders.jsx";

const auth = (user) => ({ auth: { ...makeStore().getState().auth, user } });

function renderAt(user) {
  return renderWithProviders(
    <Routes>
      <Route path="/login" element={<p>sign-in page</p>} />
      <Route path="/change-password" element={<p>password page</p>} />
      <Route element={<PrivateRoute />}>
        <Route path="/telemetry" element={<p>telemetry page</p>} />
      </Route>
    </Routes>,
    { route: "/telemetry", preloadedState: auth(user) },
  );
}

it("sends a visitor with no session to the sign-in page", () => {
  renderAt(null);
  expect(screen.getByText("sign-in page")).toBeInTheDocument();
});

it("holds a user on a temporary password at the password page", () => {
  renderAt({ id: 1, username: "ops", role: "OPERATOR", mustChangePassword: true });
  expect(screen.getByText("password page")).toBeInTheDocument();
});

it("lets a signed-in user through", () => {
  renderAt({ id: 1, username: "ops", role: "OPERATOR", mustChangePassword: false });
  expect(screen.getByText("telemetry page")).toBeInTheDocument();
});
