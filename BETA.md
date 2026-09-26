# MixMind — Beta Program

**What:** MixMind is a reference-matching telemetry shaper. Load a reference
track, overlay its spectrum against your live audio, and MixMind designs a
linear-phase FIR match-EQ to close the gap. Real-time FFT, per-group focus
colours, BS.1770 loudness / true-peak metering, session recall. It measures —
you decide.

**Requirements:** macOS 12+ and a VST3/AU host (Ableton Live or Logic Pro).
No internet connection required.

> The DeepSeek-powered chat from the July beta round was removed from the
> plugin in September 2026. It is not in any current build. Any dated artifact
> below that mentions prompts, a chat proxy, or an `MM-` gate as a feature is
> history, not the current product.

## Beta status

- Builds are shared directly with testers. There is no public download while
  the current package is rebuilt — the last public `.pkg` predates the AI
  removal.
- Pricing and licensing are under review. Do not quote a price yet.
- What we want to know: does the reference → delta → shape loop help you on a
  real mix, and where does it lie to you?

## Reddit post (draft — do not post until the build and pricing are settled)

```
Title: Reference-matching spectrum shaper — looking for beta testers

Body:

I built a plugin that compares your mix against a reference track and shapes
toward it. Load a reference, see its spectrum overlaid on your live audio, and
MixMind designs a linear-phase match EQ to close the gap. You see the delta
before you hear it, and you decide how much of it to apply.

What it does today:
  • Real-time FFT with reference vs. live overlay
  • Focus groups (bass / guitar / vocals / drums), each in its own colour
  • Auto or manual target curve, 1/3-octave smoothed, linear-phase FIR
  • BS.1770 LUFS / true peak / crest / width / phase against the reference
  • Match curve and reference saved with the session

It does not chat, it does not need an account, and it does not phone home.

macOS only for now (VST3/AU). If you want in, reply and tell me what you mix.
```

## DM template (when sending a build)

```
Hey — here's the current MixMind beta build. macOS VST3/AU, Ableton or Logic.

It's a reference-matching shaper: load a reference track, and it overlays the
reference spectrum on your live audio and builds a linear-phase match EQ.

Two things I'd love to know after you try it on a real mix:
  1. Did the reference/delta view change a decision you made?
  2. Where did the match curve disagree with your ears?
```

## What to track

| User | Build sent | Tried it? | Reference used | Notes |
|------|------------|-----------|----------------|-------|
| 1 | | | | |
| 2 | | | | |
| ... | ... | ... | ... | |

## Generating test builds

```bash
cmake -B build -G Ninja
cmake --build build --target MixMind
# Package with build_pkg.sh once the version question is settled.
```
