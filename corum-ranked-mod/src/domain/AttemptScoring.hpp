#pragma once

namespace corum::ranked {

// The server accepts at most three decimal places for Attempt End progress.
// Normalize at the transport boundary so a raw Geometry Dash float can never
// poison the serialized Start/End FIFO with a deterministic HTTP 400.
[[nodiscard]] double normalizeAttemptProgress(double progressPercent);

double calculateAttemptScore(double progressPercent, bool cleared, double qualifyingPercent);

} // namespace corum::ranked
