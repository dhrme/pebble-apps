# Pebble apps

Small apps & watchfaces for the **Pebble Time 2** (and compatible Pebble watches
running the rebooted Core Devices SDK). Hand-written C on the watch, PebbleKit JS
on the phone side. Built with [Claude Code](https://claude.com/claude-code);
released under the MIT license — fork, build, and sideload freely.

## Apps & watchfaces

| Project | What it does |
|---|---|
| [**NL Energy**](energy/) | A launchable **app**: the current Dutch dynamic electricity price (big, colour-coded), a 24-hour forward graph, and the cheapest contiguous 4-hour window. Pulls live prices from the public EnergyZero API — no account or key. |
| [**Dutch Energy Watchface**](energy-watchface/) | A glanceable **watchface**: time + date + an iPhone-style battery, a LOW/MED/HIGH price split, the current ct, and the next cheap window. Same EnergyZero feed as NL Energy. |
| [**Atelier**](atelier/) | A minimalist analog **watchface**: a clean white dial, black hands, a single orange marker at twelve, and a quiet lower band with date + battery. 4 colour themes (Atelier / Noir / Sage / Blush) in Settings. |
| [**Bauhaus Blocks**](blocks/) | A bold colour-block **watchface**: five flat bands — weekday, date, a big clock, battery, and the current temperature with today's hi/lo. Live temp from [Open-Meteo](https://open-meteo.com) (no key). 4 colour themes (Bauhaus / Noir / Pop / Earth) in Settings. |
| [**Kevinimal**](kevinimal/) | A minimalist **watchface**: large centred time on pure black, with day/date, a battery as three blocks, chance of rain, and current temp + hi/lo tucked into the corners. Open-Meteo (no key). 4 colour themes (Kevin / Daylight / Amber / Mint) in Settings. |
| [**Voice Notes**](voice-notes/) | Dictate a quick note on the watch; keep a running list. Optionally bridges a note to Google Calendar via a tiny self-hosted Google Apps Script (your own deployment, your own shared secret — see [`voice-notes/bridge/`](voice-notes/bridge/)). |
| [**Pebble Throw**](pebble-throw/) | A one-button **game**: skip a Pebble-shaped stone across the pond. **Zen** mode is endless distance skipping; **Smash** mode topples bobbing apples & androids wave by wave, with a difficulty ramp, a cross-platform combo, and full on-watch sound (the PT2 speaker). No phone, no account. |

Each app has its own `README.md` with build and setup details.

## Install

### NL Energy / Dutch Energy Watchface — prebuilt `.pbw` (no toolchain)
1. Download `energy.pbw` (the app) and/or `energy-watchface.pbw` (the watchface)
   from the [**Releases**](../../releases) page.
2. Open it with the Pebble (Core Devices) app on your phone — it sideloads onto a
   paired watch. Done; neither needs any configuration.

### Atelier / Bauhaus Blocks / Kevinimal / Pebble Throw — prebuilt `.pbw` (no toolchain)
Download the matching `.pbw` from the [**Releases**](../../releases) page and open
it with the Pebble app. Atelier and Pebble Throw need nothing; Bauhaus Blocks and
Kevinimal pull live temperature from Open-Meteo over a connected phone (no account
or key).

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
cd energy                          # NL Energy app — builds as-is (or: cd energy-watchface)
pebble build                       # → build/energy.pbw
pebble install --emulator emery    # boots the emulator + installs
pebble install --phone <phone-ip>  # or sideload to a paired watch
```
**energy** (app) and **energy-watchface** both build as-is and share the same
EnergyZero phone-side fetch. For **voice-notes**, do the bridge setup first (its
README) — `pebble build` fails until `src/pkjs/config.js` exists (copy it from
`config.example.js`).

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
