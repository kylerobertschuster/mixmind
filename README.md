# MixMind

A reference-matching spectrum shaper for macOS. Load a reference track, let the
plugin read your live spectrum, and MixMind designs a linear-phase FIR match-EQ
that moves the mix toward the reference. BS.1770-4 loudness and 4× true-peak
metering sit next to the curve, so the match and the level are visible in the
same window.

MixMind is the flagship of the JuicePipe suite — seven plugins built on one
shared DSP core: MixMind, Scope, Meter, EQT, Reflex, 3FX and Neat Neat Neat.

**Status: pre-release, no public download.** The July 2026 installer predates the
September 2026 DSP rewrite and still ships the AI chat client that was removed
from the plugin, so `site/` no longer links it. Build from source instead.

## What it does

- **Shaper** — 1024-tap linear-phase FIR, 1/3-octave-smoothed target, designed
  off the audio thread. Manual mode matches a curve you draw; Auto mode matches
  a reference file. One engine serves both.
- **Metering** — BS.1770-4 loudness (K-weighting, 400 ms gating, −70 LUFS
  absolute and −10 LU relative gates) plus 4× oversampled true peak. The same
  meter drives Scope, Meter and EQT.
- **Analysis** — 1024-bin spectrum on a 2048-point FFT, identical in the live
  and reference analyzers, so live-vs-reference subtraction needs no resampling.
- **Offline by construction** — no network code anywhere in `Source/`. The
  MixMind target builds with `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` and
  `JUCE_REPORT_APP_USAGE=0`. The September 2026 cleanup removed the AI stack
  (ApiClient, ChatComponent, ContextPanel, AIAnalysis) and left the license
  gate out of MixMind entirely.

## Build

macOS 12+, Xcode command line tools, CMake ≥ 3.22, Ninja. JUCE 8.0.6 and
Catch2 are fetched on first configure, so the first build needs network access;
after that it is offline.

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Seven targets, each producing VST3 + AU + Standalone, copied into your plug-in
folders on build (`COPY_PLUGIN_AFTER_BUILD`). AAX is deferred to V2. The build
is arm64-only today — see `docs/STATE.md`.

## Test

```bash
./build/tests/MixMind_Tests_artefacts/Release/MixMind_Tests
# or: ctest --test-dir build --output-on-failure
```

39 cases / 11921 assertions at the current tip, covering the shaper contract
(dB-mapped trace, 1/3-octave smoothing, real latency), BS.1770 loudness and true
peak against reference tones, match-state serialisation and reference-analyzer
sizing. CI runs the suite and `auval` on all seven Audio Units; see
`docs/DEMO.md` for a terminal-only walkthrough.

## Layout

```
Source/       plugin code + shared DSP (read AGENTS.md before editing shared files)
tests/        Catch2 suite
docs/         STATE.md (source of truth for current state), DEMO.md (runbook)
.pi/skills/   agent-facing DSP contract and audit checklist
proxy/        retired licence/API backend, kept for reference
site/         landing pages
```

## Working on it

- `AGENTS.md` — operating instructions: commands, boundaries, DSP and metering
  rules. Read this first.
- `pi-warden.md` — the same rules written so a machine can check them: one rule
  per heading, including the four known deviations from them.
- `docs/STATE.md` — current state, verified numbers, known deviations, open
  decisions. It is the source of truth; `AGENTS.md` is stable, this is not.

## Licence

There is no `LICENSE` file yet, so all rights are reserved by default — do not
redistribute binaries. The free-vs-paid decision is open (`docs/STATE.md`), and
it is constrained by JUCE: JUCE 8 is dual-licensed AGPLv3 or commercial, so a
closed-source paid build needs a JUCE commercial licence. MixMind itself has no
licence gate and no server dependency.
