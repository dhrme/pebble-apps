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

## Install

### NL Energy — prebuilt `.pbw` (no toolchain)
1. Download `energy.pbw` from the [**Releases**](../../releases) page.
2. Open it with the Pebble (Core Devices) app on your phone — it sideloads the app
   onto a paired watch. Done; it needs no configuration.

### Voice Notes — build it yourself
There is **no** prebuilt Voice Notes `.pbw`: your private Google Calendar bridge
URL + shared secret are compiled into the app, so each user builds their own after
a one-time bridge setup. Follow [`voice-notes/README.md`](voice-notes/README.md#setup)
(deploy the bridge → `cp config.example.js config.js` → fill it in → `pebble build`).

> Both apps target the **Pebble Time 2** (`emery`). On other Pebble models, build
> from source with that platform added — see below.

### Build from source
You need the Core Devices Pebble toolchain:
- `pebble-tool` with the `pebble` command on your PATH
- Pebble SDK 4.9.x (Core Devices) + the bundled QEMU emulator
- Node 22 for the phone-side (PebbleKit JS) builds

Install + setup instructions: <https://developer.repebble.com>. Then:

```sh
cd energy                          # NL Energy builds as-is
pebble build                       # → build/energy.pbw
pebble install --emulator emery    # boots the emulator + installs
pebble install --phone <phone-ip>  # or sideload to a paired watch
```
For **voice-notes**, do the bridge setup first (its README) — `pebble build` fails
until `src/pkjs/config.js` exists (copy it from `config.example.js`).

## Device

Pebble Time 2 = platform **`emery`** — 200×228, 64-color e-paper touch + 4 buttons,
heart rate, compass, mic. The apps target `emery`; adjust `targetPlatforms` in a
`package.json` to build for other Pebble platforms.

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
