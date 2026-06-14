# Voice Notes (Pebble Time 2)

Dictate quick notes on your wrist — and have each one automatically added to
your **Google Calendar**.

A hand-written C watchapp for the **Pebble Time 2** (`emery` platform / Core
Devices PebbleOS). Press **New Note**, speak, and the transcription is saved
locally *and* turned into a calendar event whose date/time is parsed from what
you said (e.g. "call the dentist tomorrow at 3pm").

## Features
- Speech-to-text notes via the Pebble Dictation API
- Local list of notes (newest first); tap to read, hold **SELECT** to delete
- Every note is also pushed to Google Calendar (a timed event, or all-day if no
  time is spoken)
- Green / red on-watch confirmation flash for the calendar result

## How it works
The watch can't reach the internet directly, so each note travels:

```
watch (C) --AppMessage--> phone (PebbleKit JS) --HTTPS--> Google Apps Script --> Google Calendar
```

The Apps Script (`bridge/Code.gs`) runs as **you** in your own Google account —
no OAuth client, consent screen, or tokens to manage; it just calls `CalendarApp`.

## Requirements
- [`pebble-tool`](https://github.com/pebble-dev/pebble-tool) + a Pebble SDK
  (built with Core Devices SDK **4.9.169**) and its bundled QEMU emulator
- A Google account (for the calendar bridge)
- Optional: a Pebble Time 2 to sideload onto. Most of the app runs in the
  emulator; only real speech transcription needs the watch + phone app.

## Setup

**1. Configure the phone-side bridge**
```sh
cd src/pkjs
cp config.example.js config.js      # config.js is gitignored
```
Edit `config.js`:
- `SHARED_SECRET` — any random string (`openssl rand -hex 12`)
- `SCRIPT_URL` — fill in after step 2

**2. Deploy the Google Calendar bridge**
Follow [`bridge/README.md`](bridge/README.md): paste `bridge/Code.gs` into a new
[Apps Script](https://script.google.com) project, set its `SHARED_SECRET` to the
**same** value as `config.js`, run `authorize` once, deploy as a **Web app**
(*Execute as: Me*, *Who has access: Anyone*), and copy the `/exec` URL into
`config.js` as `SCRIPT_URL`.

**3. Build & run**
```sh
pebble build
pebble install --emulator emery     # run in the emulator
# or sideload build/voice-notes.pbw onto a real Pebble Time 2
```

> **Forking?** Generate your own app UUID (`uuidgen`) and set it in
> `package.json` so it doesn't collide with this app.

## Usage
- **New Note** -> speak -> saved locally and added to your calendar.
- Tap a note to read it in full; **hold SELECT** to delete it (local only).
- Times understood: "tomorrow at 3pm", "monday morning", "in 2 hours",
  "tonight", "at 15:30", "noon"… No time -> an all-day event. Tune `parseWhen()`
  in `bridge/Code.gs`.

## Limitations (v1)
- Deleting a note on the watch does **not** remove the calendar event.
- No offline retry: offline -> the note still saves locally, the calendar add fails.
- Up to ~20 notes, ~160 characters each (Pebble local-storage cap).

## License
MIT — see [`LICENSE`](../LICENSE).
