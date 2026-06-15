# NL Energy

A Pebble Time 2 watchapp showing **Dutch dynamic electricity prices** at a
glance: the current price, a forward 24-hour graph, and the cheapest upcoming
4-hour window — so you know when to run the dishwasher.

![NL Energy on the emery emulator](preview.png)

> **Netherlands only.** Prices come from the public **EnergyZero** API (the data
> behind several Dutch dynamic-tariff suppliers), shown in **ct/kWh including
> BTW**. It is not useful outside the Dutch dynamic-tariff market. To adapt it to
> another country, swap the API and price math in `src/pkjs/index.js`.

## What's on screen
- **Big number** — the current hour's price in ct/kWh, colour-coded green (cheap)
  → yellow → red (expensive), with the current hour shown below it.
- **Bar graph** — the next 24 hours, plus ~2h of past context (greyed out). Each
  bar is colour-coded by price; the current hour's bar is outlined. The x-axis is
  labelled relative to now: `now`, `+6`, `+12`, `+18`, and the total span on the
  right.
- **Green marker** — sits under the cheapest contiguous **4-hour** block.
- **Footer** — that cheapest block's time window and its average price.

## Controls
- **SELECT** — refresh (re-fetches prices via the phone).

## How it works
No account, login, or API key. The phone side (`src/pkjs/index.js`) fetches the
forward-24h hourly series from the public EnergyZero API, picks the cheapest 4h
block, and sends a compact packet to the watch. The watch has no direct internet,
so the phone must be connected.

## Build & run
```sh
pebble build
pebble install --emulator emery       # run in the emery emulator
pebble install --phone <phone-ip>     # sideload to a paired watch (dev connection)
```
For the end-user (no toolchain) install path, see the [repo README](../README.md#install).

## Platforms
Targets **emery** (Pebble Time 2) only — the layout is tuned for its 200×228
screen. To build for other Pebble watches, add them to `targetPlatforms` in
`package.json` and expect to adjust the graph geometry.

## Documentation
Pebble SDK docs and API reference: <https://developer.repebble.com>
