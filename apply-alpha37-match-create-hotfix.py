#!/usr/bin/env python3
from pathlib import Path
import sys

TARGET = Path("ranked/apps/server/src/queue/queue.service.ts")

if not TARGET.is_file():
    print(f"ERROR: run this from the Corum-Ranked repository root. Missing: {TARGET}", file=sys.stderr)
    sys.exit(2)

text = TARGET.read_text(encoding="utf-8")
original = text

replacements = [
    (
        'readonly matchType?: "PVP" | "DEBUG_BOT";',
        'readonly matchType?: "RANKED_PVP" | "DEBUG_BOT";',
        "matchType TypeScript union",
    ),
    (
        'rules_version, match_type, debug_bot_config, discord_events_enabled, created_at',
        'rules_version, match_type, debug_bot_config, discord_events_enabled,\n'
        '         debug_config, debug_discord_events, created_at',
        "ranked_matches INSERT columns",
    ),
    (
        "$11, $11, $13, $13, $12, $14, $15::jsonb, $16, $13",
        "$11, $11, $13, $13, $12, $14, $15::jsonb, $16,\n"
        "         $15::jsonb, $16, $13",
        "ranked_matches INSERT values",
    ),
    (
        'options.matchType ?? "PVP"',
        'options.matchType ?? "RANKED_PVP"',
        "normal Ranked match_type default",
    ),
    (
        'options.discordEventsEnabled ?? true',
        'options.discordEventsEnabled ?? false',
        "normal Ranked debug event default",
    ),
]

for old, new, label in replacements:
    old_count = text.count(old)
    new_count = text.count(new)

    if old_count == 1:
        text = text.replace(old, new, 1)
        print(f"fixed: {label}")
    elif old_count == 0 and new_count == 1:
        print(f"already fixed: {label}")
    else:
        print(
            f"ERROR: {label}: expected exactly one old occurrence "
            f"(found {old_count}, already-fixed occurrences {new_count}).",
            file=sys.stderr,
        )
        print("No file was written.", file=sys.stderr)
        sys.exit(3)

required = [
    'readonly matchType?: "RANKED_PVP" | "DEBUG_BOT";',
    'options.matchType ?? "RANKED_PVP"',
    'debug_config, debug_discord_events, created_at',
    '$15::jsonb, $16, $13',
    'options.discordEventsEnabled ?? false',
]

for needle in required:
    if needle not in text:
        print(f"ERROR: validation failed; missing: {needle}", file=sys.stderr)
        sys.exit(4)

# The old PVP default must be gone. DEBUG_BOT remains valid.
if 'options.matchType ?? "PVP"' in text:
    print('ERROR: stale normal-match PVP default remains.', file=sys.stderr)
    sys.exit(5)

if text != original:
    TARGET.write_text(text, encoding="utf-8")
    print(f"\nWROTE: {TARGET}")
else:
    print(f"\nNO CHANGE: {TARGET} was already fixed.")

print("\nExpected DB behavior:")
print("  normal match_type = RANKED_PVP")
print("  Debug Bot match_type = DEBUG_BOT")
print("  legacy + alpha.37 debug columns are written together")
print("  normal debug_discord_events = false")
