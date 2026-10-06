import { createWorkspaceSaver, RETRY_MS, SAVE_DELAY_MS } from "../../../src/modules/display/utils/workspaceSaver.js";

function setup(save = jest.fn().mockResolvedValue(undefined)) {
  const states = [];
  const saveOnUnload = jest.fn();
  const saver = createWorkspaceSaver({ save, saveOnUnload, onState: (k) => states.push(k) });
  return { saver, save, saveOnUnload, states };
}

beforeEach(() => jest.useFakeTimers());
afterEach(() => jest.useRealTimers());

it("debounces changes into one save of the newest layout", async () => {
  const { saver, save, states } = setup();
  saver.queue("A");
  saver.queue("B");
  await jest.advanceTimersByTimeAsync(SAVE_DELAY_MS);
  expect(save).toHaveBeenCalledTimes(1);
  expect(save).toHaveBeenCalledWith("B");
  expect(states.at(-1)).toBe("saved");
  expect(saver.lastSaved).toBe("B");
});

it("skips a layout equal to the one the server already has", () => {
  const { saver, states } = setup();
  saver.markSaved("A");
  saver.queue("A");
  expect(states).toEqual([]);
});

it("keeps a failed layout and tries again", async () => {
  const save = jest.fn().mockRejectedValueOnce(new Error("down")).mockResolvedValue(undefined);
  const { saver, states } = setup(save);
  saver.queue("A");
  await jest.advanceTimersByTimeAsync(SAVE_DELAY_MS);
  expect(states.at(-1)).toBe("error");
  await jest.advanceTimersByTimeAsync(RETRY_MS);
  expect(save).toHaveBeenCalledTimes(2);
  expect(states.at(-1)).toBe("saved");
});

it("sends what is waiting with keepalive when the page closes", () => {
  const { saver, save, saveOnUnload } = setup();
  saver.queue("A");
  saver.flushOnUnload();
  expect(saveOnUnload).toHaveBeenCalledWith("A");
  jest.advanceTimersByTime(SAVE_DELAY_MS);
  expect(save).not.toHaveBeenCalled();
});
