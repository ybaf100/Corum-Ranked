# Survival beta.85 integrated flow

> Historical beta.85 description. The [beta.87 follow-up](survival-beta87-followup.md)
> supersedes its in-game halo/native-profile choices and tightens the visible five-second
> selected-panel hold; the result/observer/prefetch structure below remains intact.

Base main: `49119081d99b76a0b3fad3fcb48cb5be0482900a` (beta.84).

## State and authority

The server owns the initial six-second INTRO deadline, official selection, next-map certificate,
ready barrier, five-second entry hold, native ENTERED barrier, three-second native GO countdown,
rank, elimination timestamp and LP ledger. The client has explicit outcome states: choice,
local observer, early personal result and final ceremony. An early result decision remains personal
when the tournament finishes. Watch is a deliberate choice, unlocked five seconds after the
server's durable elimination timestamp; it never automatically clicks itself.

The five seconds in the selected-map panel are the existing server `entryOpensAt` barrier.
After automatic native entry, the existing three-second GO countdown still waits for native
participants. Advancing a local animation or disabling selection motion cannot authorize GO.
A manual RE-ENTER control is shown only after this round was actually entered.

## Implementation

* The initial map can be certified during INTRO, but preparation deadlines start only after 6s.
  During confirmed round results a trusted provider can durably store `nextMap`; the same
  certificate is adopted by the next round, including after process restart. Existing filters,
  recent-history advisory lock and candidate retry policy apply. No new public mutation route.
* `eliminatedAt`, `selectionOpensAt` and `nextMap` are additive live-view fields. Result roster
  after-tiers come from actual accepted human ledger results; unpaid human tiers remain null.
  Bot post-tiers are explicitly virtual and never create GD accounts. Results can settle early
  while bots continue. Explicit Debug FORCE RANK remains immediate. Forfeits retain the current
  round floor loss; no survivor or another human's attempt is fabricated.
* Observer native scenes use the existing Free Play latch and deferred native return. They never
  arm an official native launch or submit readiness, ENTERED, start, progress or end events.
  Local restarts do not alter eligibility. A changed official map returns to the retained
  coordinator for the next map; the existing map/audio verification still applies.
* A live board is a modal overlay over the same PlayLayer. Closing it preserves that scene.
  Cards and result/podium avatars use native `ProfilePage::create(accountId, false)`; virtual bots
  show a no-profile notice. Pending ranks and tiers are explicitly pending.
* Bottom-left first-place halo uses official rank, with fade/pulse and independent scene ownership.
  Observers hide that personal HUD and show the authoritative cutoff itself, not a difference.
* Personal results and final 2/1/3 podium are separate from the existing explicit LP animation.
  The redundant global LP footer and automatic result acknowledgement are removed.
* One bounded thumbnail provider/cache remains responsible for selected and random images.
  Candidate membership is per reveal identity, excluding recent candidates where possible.
  Unknown-to-known IDs and recreated owners do not reset downloads or reorder occupied reel slots.
* Persistent image-thumbnails and selection-motion options are independent. Image OFF disables
  the motion control without clearing its saved value. Independent LDM/Disable Shake defaults
  continue to seed a new round without overwriting current-round overrides.
* The selected-map view uses verified GD `GJ_starsIcon_001.png` plus the actual star count.
  Its expression-only GD difficulty frame removes the duplicate word only on this view.

## Release safety

The server stages beta.85 but advertises/requires beta.84 until BOTH channel descriptors and
public .geode HEAD responses with matching Content-Length are available. Exact beta.85 installs
can also be admitted during this transition. Promotion is monotone for the running process;
existing admitted matches/heartbeats remain unaffected. Operator package pins stay authoritative.
No previous immutable object is overwritten. Release builds now also run on PRs before merging.

## Validation evidence and limitations

The new server-flow reproduction initially failed 3/3 on the baseline and passes with the new
reducer. Native flow/observer-policy coverage includes 500 local restart checks, monotone
server-clock unlock, early-result persistence, halo eligibility and countdown boundaries.
Additional real HTTP/SQL tests cover prefetch adoption after restart, early ledger settlement,
bot-only continuation, immutable package rollout and existing Force Clear/forfeit idempotence.
Use the final run logs/CI for counts; a build is not a physical-device test.

Physical iPadOS/Windows input, native profile stacking, thumbnail texture upload/frame pacing,
actual audio continuity and 5+3-second entry must be exercised on devices. Inspect
`[SurvivalObserver]`, `[SurvivalSurrender]`, `[SurvivalReveal]` and `[RankedEntry]` markers,
and confirm no official attempt commands are generated by observer restarts. No production DB
reset or environment-variable change is part of this patch.
