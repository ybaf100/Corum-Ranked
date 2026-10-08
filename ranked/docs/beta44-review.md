# beta.44 — push review

Authoritative base: `ybaf100/Corum-Ranked` main
`e3e836b3abfd6a910dfe4c6479cf54dab8571b64` (beta.43), read through the connected
GitHub account `ybaf100`. This candidate is prepared locally; pushing requires
the user's requested final confirmation.

## Changes

- PC ESC exit: the existing native Confirm Exit setting is honored. Its Yes
  callback reaches the existing Pause `onQuit` boundary; when disabled, the
  native direct exit reaches the same boundary. Corum accesses only its tracked
  live Pause there. A separate native GD alert always uses English:
  **Surrender Match?**, **Leaving will surrender this match and count as a loss.
  Do you want to leave?**, **Stay / Surrender**. Surrender submits scope MATCH;
  authenticated ACK and the existing attached-Pause native quit handoff remain
  the authority to leave gameplay. Cancel, timeout, scene exit and context
  changes clear only this dialog. Pause retirement also cancels a still-open
  original GD Confirm Exit by its exact Pause delegate before Resume, so the
  30-second budget cannot leave a dangling native confirmation. Ordinary
  GD/Placement exit stays native.
- Debug TRIGGER DEATHMATCH: real native-client Attempt starts no longer cause
  simulated player clears to be silently skipped. The development scenario ends
  that exact already accepted Attempt through MatchService, verifies durable
  clear counts, uses stable step event IDs, and resumes after rejection/lost ACK
  without repeating a completed prefix. Normal PvP scoring and native scene
  entry barriers are unchanged.
- Player card top-left **INFO / 정보** opens a Corum information card. GD's
  native user-info cache/delegate supplies Global Rank (only positive), Stars,
  Moons, Demons and Extreme Demons. Extreme totals combine classic/platformer
  counters and exclude weekly/gauntlet duplicates. Queries respect foreign
  delegates, validate GD account IDs, stop on exit and have bounded retries.
  Owned information/settings/Jukebox dialogs close before automatic entry.
- **GDKF #n / Pointercrate #n** use a new durable cache, initialized after the
  profile transaction commits. Reads never trigger external refreshes. First
  creation jobs can finish while the information card is open; a bounded poll
  reads only the database. Settings provides **Refresh forum records / 포럼 기록
  갱신**. Service-level durable 60-second refresh leases deduplicate requests;
  errors retain the last good rank with an error marker. CoJong is initialized
  at server startup and included in explicit refresh targets independently of
  Ranked membership. Bot information uses CoJong's real GD profile, without
  changing the bot's gameplay identity.
- Ban/preparation/history map titles preserve their full original strings.
  Cocos wraps UTF-8 glyphs and scales the entire block into reserved bounds.
  History detail scrolls all rounds/deathmatches and shows each full map title.
- Client/server exact gate, workspace versions and lockfile agree on
  `v0.5.0-beta.44`. Protocol v2 and gameplay transport are unchanged. Korean
  labels have matching font charset additions. External image pack inputs did
  not change; automatic Debug CI does not repackage that pack.

## Evidence and limits

The supplied Windows crash report places Corum offsets `+0x7328c` and `+0x76068`
between native `FLAlertLayer::onBtn2` and `CCNode::getFieldContainer` (null read at
`0x68`). The exact beta.43 Windows artifact disassembly maps those offsets to
the Pause hook's Fields lookup. The patch removes dependency on the incoming
native callback receiver. The GD Windows executable body was not obtained;
this is not a claim that every instruction of the native caller was verified.

The previous successful device Pause lifecycle remains covered by the existing
concession ordering, ownership, refusal and retained-object suites. New exit
tests exercise the actual Corum boundary via a free ABI adapter with null and
invalid incoming receiver values (not undefined C++ null-member calls), and the
actual English dialog callbacks with native UI doubles. New information tests
compile the full popup against GD/network/UI doubles. These are host tests,
not real Windows/iPad verification or a complete Geode platform build.

The Death Match PostgreSQL integration enters every round through the native
scene certificate, admits the real player's Attempt before the scenario tick,
verifies three actual 3-clear draws, and reaches DEATHMATCH_PLAYING with live bot
progress. Additional checkpoint tests cover a committed End with a lost ACK and
a refused Start. The uploaded crash report alone has no Death Match timeline.

## External APIs and identity

- [GDKF official API](https://gmdkoreaforum.com/api-docs):
  `/api/users/name/{name}?config=demonlist`, then cached `/id/{id}`.
- [Pointercrate ranking API](https://pointercrate.com/documentation/players/#get-ranking):
  `/api/v1/players/ranking/?name_contains={name}`. The source follows validated
  same-origin next Link cursors, not ranks; `name=` is not a ranking filter.
- Public GETs require no configured API key. Fixed JSON Accept/User-Agent,
  request timeouts, bounded pagination and 429 Retry-After are applied.
- A GD name and an external alias are not a universal verified identity key.
  Only one exact canonical candidate is linked; leading Pointercrate team tags
  are normalized, arbitrary substring matches are rejected, ambiguous matches
  show **Not linked**. External IDs persist once linked. Alias changes that no
  longer match the search can require operator mapping; do not fabricate ranks.
- CoJong reference IDs: GD account **16792985**, GDKF **3251**, Pointercrate
  **60108**. Both forum ranks and the native GD public profile were successfully
  read during investigation; those ranks are snapshots, not fixed constants.

## Validation before approval

- Rules: 114 tests passed; complete server suite: 216 tests passed, followed by
  two checkpoint regressions, the after-commit profile initialization regression,
  and focused forum/Death Match/session reruns.
- Type-check and server/rules build passed.
- Source/native-boundary suites: 121 passed, including new exit/information/full
  title tests, followed by the exact native-alert retirement regression. Latest
  information-popup and Pause/concession follow-ups also passed.
- Nine strict C++ suites, five asset-build policy tests and existing Apps Script
  configuration/record-sheet compatibility checks passed.
- `git diff --check` passed. No remote writes, manual Release, asset release or
  device test has been performed for this candidate.

After approval, re-read remote main, construct the commit from the current
remote tree through connected ybaf100, and update the branch without force.
Run normal automatic Debug CI (Windows/Android32/Android64/iOS Debug ON/OFF and
combined package validation). Real Windows checks still cover Confirm Exit
ON/OFF, both Cancel choices, English warning with Korean UI, accepted surrender,
next-round/new-match Pause, and retained modal cleanup. Real device acceptance
must remain distinct from CI/host results.
