# Badge presentation follow-up (beta.117)

The gallery footer now displays each difficulty's owned total using the same
six-grade official count sum as its detail header. The final red-X entry uses
the global ELIMINATED total. Zero remains visible, and the existing validation
and request-failure states prevent a failed request from becoming a zero count.
Detail names use the server's English difficulty names, with an English fallback
for legacy difficulty IDs in the ELIMINATED breakdown.

Previously each face was scaled to its own full effect bounds. Larger ornaments
therefore shrank the face body, making Rated visibly larger than the other
grades. The common square factory now measures the native difficulty/effect
envelope once in unfeatured-frame height units, across GD's native codes -1..10
and all five feature states. Every displayed body uses the same height, capped
at 46% of its available region. All states reserve the largest ornament envelope;
their own effect bounds no longer determine their individual scale. Body centers
share a baseline, while native child positions, transforms and animations remain
owned by GD. No sprite canvas or ornament offsets are modified.

Personal result and history cards reuse that same factory with dark AP-style
surfaces and grade accents. Official round/rank captions retain separate space,
and elimination remains a red X. Personal cards are square and use up to three
columns, with a centered last row; the existing sequential motion is retained.
History uses the persisted difficulty, not current map feature metadata. Missing
historical difficulty displays GD's unknown face rather than inventing metadata.
No grade, LP/AP, persistence, result flow, thumbnail/level actions, collection
eligibility policy, five-slot drag behavior or ELIMINATED ordering changes.

Boundary tests execute production sizing and card functions against native API
and renderer doubles, including differing base sizes, five effects, transformed
ornaments, reserved caption margins, authoritative captions/counts, and 4:3/mobile
layout. They do not constitute actual GD rendering or touch verification.
