# Pebble apps

Two small apps for the **Pebble Time 2** (and compatible Pebble watches running
the rebooted Core Devices SDK). Hand-written C on the watch, PebbleKit JS on the
phone side. Built with [Claude Code](https://claude.com/claude-code); released
under the MIT license — fork, build, and sideload freely.

## Apps

| App | What it does |
|---|---|
| [**NL Energy**](energy/) | Shows the current Dutch dynamic electricity price, a 24-hour forward graph, and the cheapest contiguous 4-hour window. Pulls live prices from the public EnergyZero API — no account or key. |
| [**Voice Notes**](voice-notes/) | Dictate a quick note on the watch; keep a running list. Optionally bridges a note to Google Calendar via a tiny self-hosted Google Apps Script (your own deployment, your own shared secret — see [`voice-notes/bridge/`](voice-notes/bridge/)). |

Each app has its own `README.md` with build and setup details.

## Device

Pebble Time 2 = platform **`emery`** — 200×228, 64-color e-paper touch + 4 buttons,
heart rate, compass, mic. The apps target `emery`; adjust `targetPlatforms` in a
`package.json` to build for other Pebble platforms.

## Toolchain

- `pebble-tool` (Core Devices) — `pebble` on your PATH
- Pebble SDK 4.9.x, QEMU emulator bundled
- Node 22 for the phone-side (PebbleKit JS) builds

## Build & run

```sh
cd energy            # or voice-notes
pebble build
pebble install --emulator emery   # boots the emulator + installs
# sideload the resulting build/*.pbw on a real watch to confirm sensors/feel
```

## Per-app layout

```
<app>/
  package.json       # name, uuid, targetPlatforms
  src/c/<app>.c      # watch logic (C)
  src/pkjs/index.js  # phone-side JS (settings, web) — optional
  resources/         # fonts, images
```

## License

MIT — see [LICENSE](LICENSE).
