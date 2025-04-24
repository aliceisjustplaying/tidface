module.exports = {
  preset: "ts-jest",
  testEnvironment: "node",
  roots: ["<rootDir>"],
  testMatch: ["**/*.test.ts"],
  moduleFileExtensions: ["ts", "js", "json", "node"],
  // Transform TypeScript files using ts-jest
  transform: {
    "^.+\\.tsx?$": [
      "ts-jest",
      {
        diagnostics: false,
      },
    ],
  },
  // Enable detectOpenHandles to report and close any lingering handles
  detectOpenHandles: true,
};
