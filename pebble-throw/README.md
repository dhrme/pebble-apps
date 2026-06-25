# Pebble Throw

Skip a Pebble-shaped stone across the pond on your Pebble Time 2 (emery). One
button, two taps, endless "one more throw." Two modes, and real sound through the
watch speaker.

## Controls

On the **title**, each right-hand button is labelled on screen:

- **up** → `mode` — cycle Zen ↔ Smash (the current mode shows next to the button)
- **select** → `Play`
- **down** → sound on/off (the speaker icon shows the state)

In a throw, **`select`** does everything; **`back`** steps out to the title.

1. **Aim** — the angle needle sweeps; `select` locks it. Sweet spot ≈ 20°.
2. **Power** — the power bar oscillates; `select` throws at the right moment.
3. **Skip!** — the stone arcs and skips; the full path holds for a beat so you can
   read it. Each skip/hit buzzes and plays a tone.

High angle = splash (`plop!`). Low power = it sinks. The **wind** (shown while you
aim) nudges distance ±3.

## Sound

Full audio on the PT2 speaker: a throw whoosh, a rising plink per skip, apple/android
smash tones, a chord on a combo, splash, win fanfare, and a game-over riff. Toggle
with the **down** button on the title (persists).

## Zen mode

Endless distance skipping. Go far, beat your best. Score = skips + metres, with a
star rating. Bests persist.

## Smash mode — topple the giants

The little guy vs the duopoly. Skip the stone *through* rows of **apples** and
**androids** bobbing on the pond — every bounce that clips one topples it. Score is
counted in apples + androids. Hit both types in one throw = **cross-platform combo**.

Wave-based, and it ramps:

| Knob | Per wave |
|------|----------|
| Targets | more, spread farther out |
| Throw budget | tightens (`targets − wave/2`, min 2) |
| Meter speed | angle + power sweep faster |

Clear every giant in the wave within your throws → next wave. Run out → game over.
Best wave + best toppled persist. (Phase 2 ideas: moving targets, armoured apples,
bosses, decoy Pebbles, hazards.)

## Scoring (Zen)

- Skips = `power × angle-quality`, peaking at the 20° sweet spot, ±1 luck.
- Distance ≈ `skips × 12 + power/3 + wind × 3`.

## Build

    export PATH="$HOME/.local/bin:$PATH"
    pebble build < /dev/null
    pebble install --emulator emery < /dev/null

No phone JS, no custom resources — system fonts only.
