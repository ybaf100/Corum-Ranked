# Claude Code session entry point

Read `AGENTS.md`, then `docs/agent-transfer/AGENT_HANDOFF.md`,
`SESSION_TRANSCRIPT.md` and `VERIFICATION.md` before acting.

The latest user request overrides the standing deployment default: this task is
handoff preparation only. Do not merge to main, publish packages, deploy, bump
versions or implement new features. Remain on the handoff branch unless the user
explicitly authorizes subsequent implementation. Preserve all existing work.

Fetch origin/main before future code work. Recheck actual code and deployment;
historical reports are evidence of past checks, not current health guarantees.
Do not include credentials in documentation, tool output or commits. Do not
spawn subagents without explicit user/project authorization.

The transfer transcript is explicitly incomplete; distinguish quoted messages,
summaries, source attachment text, confirmed facts and hypotheses. Never invent
unavailable turns or claim actual GD/device validation from tests/builds.
