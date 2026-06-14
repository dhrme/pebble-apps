# Voice Notes → Google Calendar bridge

`Code.gs` is a Google Apps Script **web app** that turns each dictated note into
a Google Calendar event. It executes **as you**, so there is no OAuth client,
consent screen, client secret, or token refresh to manage.

## Deploy (once, ~5 min)

1. Open <https://script.google.com> → **New project**.
2. Delete the sample code, paste the contents of `Code.gs`, and **Save**.
3. Set `SHARED_SECRET` (top of `Code.gs`) to a random string, and put the
   **same** value in `../src/pkjs/config.js`.  (`openssl rand -hex 12`)
4. In the editor, choose the **`authorize`** function and click **Run** once;
   approve the Calendar permission prompt. (One time only.)
5. **Deploy ▸ New deployment ▸** gear icon ▸ **Web app**:
   - **Execute as:** Me
   - **Who has access:** Anyone   ← the shared secret is what protects it
   - **Deploy**.
6. Copy the **Web app URL** (ends in `/exec`) and put it in
   `../src/pkjs/config.js` as `SCRIPT_URL`, then rebuild the watch app.

To rotate the secret later, change it in both `Code.gs` (redeploy) and
`config.js` (rebuild).

## What the parser understands
`tomorrow at 3pm` · `monday morning` · `in 2 hours` · `tonight` · `at 15:30` ·
`noon` · `day after tomorrow`. No time detected → an **all-day** event that day.
Tune `parseWhen()` in `Code.gs`.

## Known v1 limits
- Deleting a note **on the watch** is local-only; it does not delete the event.
- No offline retry queue.
