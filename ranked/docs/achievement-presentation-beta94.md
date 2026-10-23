# Achievement UI beta.94

The previous beta.92 presentation used static platinum `#E5E4E2` for V/Mythic card accents and static progress bars. In beta.94 the V/Mythic **card accent** changes to diamond blue `#8FE1FF`; all other stage/rarity colors remain unchanged. Progress bars for tier 5 use the existing shader's flowing seven-pastel rainbow (`achievementPastelAt` fallback); lower stages' bars, Claim All and other controls remain static.

The client-only visual change does not affect achievement metrics, claims, title eligibility, AP, server data, or Neon schema. Verify unlocked and locked MYTHIC card accents, equipped progressive stage V, and animated rainbow fill including a partially filled bar on iOS/Android/Windows.

## Follow-up: restore achievement v1 bright diamond

The achievement v1 level V color in `ranked/packages/rules/src/achievements.ts` at pre-v2 commit `528de46950e96ee5c8d970f9fdffea9fa594ba26` was `#8DEBFF` (RGB 141,235,255), and the v1 card UI read it as its accent color. Restore **that original value** for V/Mythic card accents instead of beta.94's `#8FE1FF` (RGB 143,225,255). The seven-pastel animated progress bar remains unchanged, as do lower tiers, achievements, rewards, backend, and database.
