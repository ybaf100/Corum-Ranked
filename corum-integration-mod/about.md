# Corum Integration

Displays the Corum difficulty as a color-coded card beside the Geometry Dash difficulty, with the current Corum rank and maximum points for a 100% record above it. The card uses Geometry Dash text and one matching color for both its fill and border. Its text automatically switches between near-black and white using the rating background luminance.

A paper-plane button appears in the upper-left on every Corum-listed level. Before your first submission, the `Submit Record` popup shows estimated `CURRENT` and `MAX` point values using the website's `corum-v1` formula. After a record exists, its awarded score stays locked while the saved best is unchanged. When your local best is higher than the server best, the popup previews the `NEW` score that will be awarded using the current rank, minimum, and improved percentage. Rank or minimum changes alone do not change a locked score. The Submit button is disabled until your best meets the requirement.

The popup remains open during submission. It shows a spinner while sending, Geometry Dash's complete icon after success, or its delete icon with the error details after failure.

Your current Geometry Dash username and best record are sent when you submit a record. You can submit again after improving your best.

The Geometry Dash main menu also has a Corum paper-plane button for batch
submission. Opening it refreshes the full Corum catalog and this account's server records, scans
all saved local bests, and shows only records that meet the current minimum and
beat the server best. The review list shows each map's percentage, expected
award, 100% maximum, and the expected player total. Nothing is sent until
`Submit All` is pressed. Every confirmed record is then sent in one batch
request. The popup keeps its animated loading view open until the server returns
one response, which is expanded into a final per-map success or failure list.

Your current Geometry Dash account ID and username are registered automatically on the first submission. No personal token is required.

At game startup, the mod reads the current Google Apps Script endpoint from the
website endpoint manifest and downloads the complete Corum map catalog once.
`C Integration is ready` appears only after both steps finish. A startup error
notification appears if either step fails.

If the server marks the installed platform version as unsupported, the mod shows
`C Integration is outdated!`. Level information remains available, while both
single-record and batch submission are blocked. The Update button opens the
official Corum Integration GitHub Releases page.

The startup map snapshot is kept for the rest of the game session, so opening
level screens does not trigger background refreshes. Opening the paper-plane
popup is the one exception: it first shows a loading view, then fetches that
map's latest rank, minimum, existing record, current points, and maximum points
before showing the submission form.

When a map has a different `대체 맵 코드`, Corum information and submission
work on both level IDs. The main-menu scan compares both saved levels and keeps
only the higher local best. The server normalizes either submission to the
primary map so records and points are never counted twice.

No URL input or mod rebuild is required when the Apps Script deployment changes.
Update `public/corum-endpoint.json` on the Corum website and redeploy the site.

Corum information and the paper-plane button stay hidden on levels that are not listed on Corum.

When you enter the custom-level menu from the main menu, a warning explains that C Integration is active. Press `Continue` to proceed or `Cancel` to stay on the main menu.

Submitted records are stored as `unverified`. Corum Integration v0.2.29 no longer depends on or calls CheatAPI.

When record submission is enabled, Corum Integration reads the IDs and versions of loaded non-internal Geode mods once at startup and includes that list with record verification metadata.

Eligible 100% Corum clears may create a lossless PNG of Geometry Dash's completed End Level scene, including the current Geometry Dash username and map title. The image is stored locally first and is uploaded only after the player presses Submit or Submit All. It does not capture other applications or operating-system UI, and no capture notification is shown during gameplay. After a record and its evidence are accepted, the local pending PNG is deleted; the server copy is retained for private record verification. A missing or unusable PNG does not prevent the record itself from being submitted.

Turning off the `Record submission` setting disables the Corum record controls and automatic preparation of new End Level verification images. Install or use the mod only after reviewing the download notice and the bundled `PRIVACY.md`.
