#!/usr/bin/env bash
set -euo pipefail
# Run from repository root after extracting the alpha.6 source overlay.
# The legacy test path is intentionally kept and overwritten by alpha.6, so no deletion is required.
# Remove the old alpha.4 duplicate DebugBotPopup source path if it survived from a historical working tree.
rm -f corum-ranked-mod/src/debug/DebugBotPopup.cpp corum-ranked-mod/src/debug/DebugBotPopup.hpp
echo "alpha.6 overlay cleanup complete"
