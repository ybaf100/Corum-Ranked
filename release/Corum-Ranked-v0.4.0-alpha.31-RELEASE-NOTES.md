# Corum Ranked v0.4.0-alpha.31

- Fixed the extra `STARTS IN 10` screen after a player reaches two Clears; LAST ATTEMPT now switches that player directly to the opponent spectator screen.
- Added `WAITING TO START` state and remaining LAST ATTEMPT start-window display before the opponent begins.
- Fixed Debug Bot Matches becoming stuck forever in `ROUND_SETTLING` when the Bot's last attempt crossed the 10-second start deadline.
- Bot attempts accepted before the deadline now continue naturally until death/Clear, exactly like normal Ranked attempts.
- Added recovery for a live Bot attempt after a development server restart during settling.
- Ranked scoring/MMR and the actual 10-second start-deadline rules are unchanged.
