import { errorMessage, toRejection, UNREACHABLE } from "../../../src/api/apiError.js";

const axiosError = (status, data) => Object.assign(new Error("Request failed"), { isAxiosError: true, response: status ? { status, data } : undefined });

describe("toRejection", () => {
  it("prefers the backend's own { error } text", () => {
    expect(toRejection(axiosError(409, { error: "Name already taken" }), "fallback")).toEqual({
      status: 409,
      message: "Name already taken",
    });
  });

  it("says the backend is unreachable when no response came back", () => {
    expect(toRejection(axiosError(0), "fallback")).toEqual({ status: 0, message: UNREACHABLE });
  });

  it("uses the caller's words for an HTTP error without a body", () => {
    expect(errorMessage(axiosError(500, ""), "Could not save.")).toBe("Could not save.");
  });

  it("passes on the message of an Error the code threw itself", () => {
    expect(errorMessage(new Error("Stack refused to start"), "fallback")).toBe("Stack refused to start");
  });
});
