import { fireEvent, screen, waitFor } from "@testing-library/react";
import authApi from "../../../src/api/authApi.js";
import Login from "../../../src/pages/Login.jsx";
import { makeStore } from "../../../src/redux/store.js";
import { renderWithProviders } from "../../utils/renderWithProviders.jsx";

jest.mock("../../../src/api/authApi.js", () => ({
  __esModule: true,
  REFRESH_URL: "/auth/refresh",
  default: { refresh: jest.fn(), login: jest.fn(), logout: jest.fn(), changePassword: jest.fn() },
}));

const fill = (label, value) => fireEvent.change(screen.getByLabelText(label, { exact: false }), { target: { value } });

it("signs in with the username and password typed", async () => {
  authApi.login.mockResolvedValue({ id: 1, username: "ops", role: "OPERATOR", mustChangePassword: false });
  const { store } = renderWithProviders(<Login />, { route: "/login" });

  const button = screen.getByRole("button", { name: "Sign in" });
  expect(button).toBeDisabled();
  fill("Username", "  ops ");
  fill("Password", "a long passphrase");
  fireEvent.click(button);

  await waitFor(() => expect(store.getState().auth.user?.username).toBe("ops"));
  expect(authApi.login).toHaveBeenCalledWith("ops", "a long passphrase");
});

it("shows why a sign-in was refused and clears the password", async () => {
  authApi.login.mockRejectedValue({ isAxiosError: true, response: { status: 401, data: { error: "Wrong username or password" } } });
  renderWithProviders(<Login />, { route: "/login" });
  fill("Username", "ops");
  fill("Password", "nope");
  fireEvent.click(screen.getByRole("button", { name: "Sign in" }));

  expect(await screen.findByText("Wrong username or password")).toBeInTheDocument();
  expect(screen.getByLabelText("Password", { exact: false })).toHaveValue("");
});

it("offers a retry, not a sign-in form, when the backend never answered", () => {
  const preloadedState = {
    auth: { ...makeStore().getState().auth, bootUnreachable: true, error: "The MCC backend is not reachable." },
  };
  renderWithProviders(<Login />, { route: "/login", preloadedState });
  expect(screen.getByText("The MCC backend is not reachable.")).toBeInTheDocument();
  expect(screen.getByRole("button", { name: "Try again" })).toBeInTheDocument();
  expect(screen.queryByLabelText("Username", { exact: false })).not.toBeInTheDocument();
});
