from pathlib import Path

path = Path('.github/workflows/build-ranked-mod.yml')
text = path.read_text(encoding='utf-8')
old_a = '              "This is the only live-game response allowed to mutate",\n'
new_a = '              "provisionalMapGrade",\n'
old_b = '              "Progress is intentionally best-effort telemetry",\n'
new_b = '              "m_view.placement.currentMapGrade = provisionalGrade.unwrap()",\n'
if old_a not in text:
    raise SystemExit('old Placement mutation invariant not found')
if old_b not in text:
    raise SystemExit('old Placement progress invariant not found')
text = text.replace(old_a, new_a, 1).replace(old_b, new_b, 1)
path.write_text(text, encoding='utf-8')
print('Updated Build Corum Ranked Mod realtime Grade invariants')
