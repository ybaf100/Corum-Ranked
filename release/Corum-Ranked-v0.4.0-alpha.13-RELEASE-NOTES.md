# Corum Ranked v0.4.0-alpha.13

- LAST ATTEMPT no longer exits the level when the 10-second start window closes while an accepted attempt is still active.
- Trigger-side spectator view stays active through `ROUND_SETTLING` until that accepted attempt ends.
- HUD score now shows a provisional live score once an active attempt reaches Qualifying; authoritative score is still committed only when the attempt ends.
- Debug Bot defaults were nerfed: longer delay between attempts, lower qualifying/clear chances, and slower progress speed on all difficulties.
