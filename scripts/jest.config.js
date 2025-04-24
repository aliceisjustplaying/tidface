module.exports = {
  preset: "ts-jest",
  testEnvironment: "node",
  roots: ["<rootDir>"],
  testMatch: ["**/*.test.ts"],
  moduleFileExtensions: ["ts", "js", "json", "node"],
  // Transform TypeScript files using ts-jest
  transform: {
    "^.+\\.tsx?$": "ts-jest",
  },
  // Silence verbose warnings from ts-jest about diagnostics for faster runs
  globals: {
    "ts-jest": {
      diagnostics: false,
    },
  },
};
