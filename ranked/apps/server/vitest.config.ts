import { defineConfig } from "vitest/config";

export default defineConfig({
  test: {
    // Each database-backed suite boots an in-process PostgreSQL WASM runtime.
    // Running several instances at once is slower and less deterministic than
    // executing the files serially, especially on constrained CI runners.
    fileParallelism: false,
  },
});
