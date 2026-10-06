import authApi from "../../../src/api/authApi.js";
import { makeStore } from "../../../src/redux/store.js";
import { restoreSession, sessionEnded, signIn, signOut } from "../../../src/redux/auth/index.js";

jest.mock("../../../src/api/authApi.js", () => ({
  __esModule: true,
  REFRESH_URL: "/auth/refresh",
  default: { refresh: jest.fn(), login: jest.fn(), logout: jest.fn(), changePassword: jest.fn() },
}));

const USER = { id: 1, username: "ops", displayName: "Ops", role: "OPERATOR", mustChangePassword: false };
const httpError = (status) => Object.assign(new Error("x"), { isAxiosError: true, response: status ? { status, data: {} } : undefined });

describe("restoreSession", () => {
  it("signs the user back in from the refresh cookie", async () => {
    authApi.refresh.mockResolvedValue(USER);
    const store = makeStore();
    await store.dispatch(restoreSession());
    expect(store.getState().auth.user).toEqual(USER);
    expect(store.getState().auth.bootStatus).toBe("succeeded");
  });

  it("treats a 401 as 'nobody signed in', not an error", async () => {
    authApi.refresh.mockRejectedValue(httpError(401));
    const store = makeStore();
    await store.dispatch(restoreSession());
    expect(store.getState().auth).toMatchObject({ user: null, bootStatus: "succeeded", bootUnreachable: false, error: "" });
  });

  it.each([0, 502])("marks the backend unreachable on status %i", async (status) => {
    authApi.refresh.mockRejectedValue(httpError(status));
    const store = makeStore();
    await store.dispatch(restoreSession());
    expect(store.getState().auth.bootUnreachable).toBe(true);
    expect(store.getState().auth.error).toMatch(/not reachable/);
  });

  it("explains a backend built before sign-in existed", async () => {
    authApi.refresh.mockRejectedValue(httpError(404));
    const store = makeStore();
    await store.dispatch(restoreSession());
    expect(store.getState().auth.error).toMatch(/older build without sign-in/);
  });
});

describe("signIn / signOut", () => {
  it("shows the server's reason when sign-in is refused", async () => {
    authApi.login.mockRejectedValue(Object.assign(httpError(401), { response: { status: 401, data: { error: "Wrong username or password" } } }));
    const store = makeStore();
    await store.dispatch(signIn({ username: "ops", password: "nope" }));
    expect(store.getState().auth).toMatchObject({ user: null, signInStatus: "failed", error: "Wrong username or password" });
  });

  it("signs out locally even when the server call fails", async () => {
    authApi.login.mockResolvedValue(USER);
    authApi.logout.mockRejectedValue(httpError(0));
    const store = makeStore();
    await store.dispatch(signIn({ username: "ops", password: "right" }));
    await store.dispatch(signOut());
    expect(store.getState().auth).toMatchObject({ user: null, signedOutByUser: true });
  });

  it("an expired session is not a deliberate sign-out", () => {
    const store = makeStore({ auth: { ...makeStore().getState().auth, user: USER } });
    store.dispatch(sessionEnded());
    expect(store.getState().auth).toMatchObject({ user: null, signedOutByUser: false });
  });
});
