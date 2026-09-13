# Corum Ranked v0.5.0-beta.16

## Achievement System v1

- Adds ten five-level progressive achievements and eleven one-shot achievements from one shared, config-driven source of truth.
- Unlocks are derived only from existing server-authoritative Ranked, Death Match, Promotion, tier, LAST ATTEMPT, PERFECT WIN, and Placement results.
- Rewards remain `UNCLAIMED` until the player presses `CLAIM`. The server atomically awards every pending level for that achievement through a unique append-only AP ledger, so retries cannot duplicate AP.
- Existing accounts whose Placement run completed normally receive the one-shot `첫 걸음` unlock retroactively. Skipped Placement is excluded when the persisted completion kind distinguishes it.
- The Trophy screen shows total AP, pending reward count, I–V progress, rarity, available AP, Claim controls, and equipable Achievement Titles. Progressive titles may use any unlocked historical version; claiming is not required.
- Unlock and level-up notices are queued above Match Result or Placement Result only. Ordinary progress and post-V progress never produce a toast.

## Death Match HUD

- Local and opponent progress-bar icons use `max(authoritative committed best, valid live progress)` for the current Death Match map.
- Icons never move backward within that map, restore from authoritative best state after refresh/reconnect, and reset with a new Death Match map after a tie.
- This is presentation-only; the three scored Attempts, best-raw-progress winner calculation, and tie rematch rules are unchanged.

## Storage and API

- Adds migration `0010_achievements_v1.sql` for per-player metrics, unlocks, event deduplication, AP ledger entries, cached total AP, and equipped title identity/version.
- Adds authenticated state, Claim, title-equip, and title-clear endpoints with server-side achievement and level validation.
- AP amounts are snapshotted at unlock/award time, preserving historical awards if future config values change.

## Release policy

- The exact client/server gate is `v0.5.0-beta.16`.
- Routine pushes validate through the automatic Ranked and native Debug workflows. The Release workflow remains manual-only and is not part of this change.
