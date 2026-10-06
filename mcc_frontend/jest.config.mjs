/** @type {import('jest').Config} */
const config = {
  clearMocks: true,
  collectCoverageFrom: ["src/**/*.{js,jsx}", "!src/main.jsx"],
  coverageDirectory: "coverage",
  moduleNameMapper: {
    "\\.(css|less|scss)$": "identity-obj-proxy",
    "\\.(png|jpg|jpeg|gif|svg|webp)$": "<rootDir>/test/mocks/fileMock.js",
  },
  roots: ["<rootDir>/test"],
  setupFilesAfterEnv: ["<rootDir>/test/setup/jest.setup.js"],
  testEnvironment: "jsdom",
};

export default config;
