# Pebble apps — agent notes

Personal Pebble Time 2 workspace. Human overview: README.md.

## Working model
User supplies the idea (the "what"). Claude: design → code → `pebble build` → run in
the **emery** emulator → `pebble screenshot` → iterate. User sideloads the `.pbw` on the
real watch for sensor/feel/battery checks. Watchfaces can use the Core Devices skill
(`coredevices/pebble-watchface-agent-skill`); full apps are hand-written C.

## Device
Pebble Time 2 = platform **emery** — 200×228, 64-color e-paper touch + 4 buttons (back,
up, select, down), HR, compass, mic. Siblings: gabbro (Time 2 Round), flint (Pebble 2 Duo).
Voice **dictation** (`dictation_session_*` → speech-to-text string; there is **no** raw-audio
capture API) is confirmed working on the real watch via the Core Devices phone app (2026-06).

## Toolchain (installed 2026-06)
- `pebble` = pebble-tool 5.0.36 via uv at `~/.local/bin/pebble`. Put it on PATH first:
  `export PATH="$HOME/.local/bin:$PATH"`
- Pebble SDK 4.9.169 (Core Devices), QEMU bundled. Node 22 for PebbleKit JS.

## CLI gotchas (learned the hard way)
- **Run every `pebble` command with `< /dev/null`.** Otherwise first-run/interactive
  prompts block on stdin and the command hangs until timeout (symptom: no output, no
  `build/` dir created).
- **Emulator cold-start race:** the first `pebble install --emulator emery` after a cold
  boot can fail `[Errno 61] Connection refused` (QEMU + pypkjs not ready yet) — just
  retry once and it connects.
- Screenshot: `pebble screenshot out.png --emulator emery --no-open` → 200×228 PNG.
- `qemu-pebble` busy-loops near 100% CPU — run `pebble kill` when done.
- Build prints benign `LOAD segment with RWX permissions` linker warnings; ignore them.
- **Wedged emulator → `pebble wipe`.** After churn (repeated kill/reinstall, or the
  `transcribe` dance below) `screenshot`/`ping` start timing out *even though* `install`
  still succeeds and `qemu-pebble` is alive — the persistent flash (`qemu_spi_flash.bin`)
  is corrupted. `pebble kill` alone does NOT fix it; do `pebble kill && pebble wipe`, then a
  fresh `install` recovers. (`wipe` also clears the watch's stored data, e.g. app persist.)
- **Test the Dictation/voice flow headlessly:** `pebble emu-button click select` starts a
  session, then `pebble transcribe "text"` injects a transcript (or
  `pebble transcribe --error connectivity|disabled|no-speech-detected` injects a failure).
  `transcribe` holds the connection open and often never exits — `pkill -f "pebble transcribe"`
  between runs, or it wedges the bridge (see above).
- **Simulate a long-press:** `pebble emu-button push <btn>`, then (after >500 ms, e.g. as a
  separate command) `pebble emu-button release <btn>`. Do NOT use `click --duration` — it
  does not produce a real hold, so a `window_long_click_subscribe` handler never fires.

## Per-app layout
    <app>/
      package.json       # uuid, targetPlatforms: ["emery", ...]
      src/c/main.c       # watch logic (C)
      src/pkjs/index.js  # phone-side JS (settings/web) — optional
      resources/         # fonts, images
