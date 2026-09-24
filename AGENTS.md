# AGENTS.md

Operating instructions for the Pi coding agent working in `mixmind`.
Keep this file at the repository root.

## Purpose

MixMind is a JUCE-based spectrum analyzer / reference-matching shaper
plugin. The flagship goal is FabFilter / Ozone 8 caliber: honest DSP,
honest metering, no proxy or licensing scaffolding in the audio path.
Prefer correctness over cleverness. When a request is ambiguous, ask —
do not choose a definition and proceed.

## Commands

```bash
cmake -B build -G Ninja        # configure (JUCE 8.0.6, C++17)
cmake --build build            # build all targets
cmake --build build --target MixMind_Standalone
./build/MixMind_Standalone_artefacts/Standalone/MixMind   # run standalone
```

Targets: `MixMind`, `Scope`, `Meter`, `EQT`, `Reflex`, `ThreeFX`, `Neat`.
AAX is deferred to V2. Do not add AAX to the CMake target lists.

## Boundaries

Do:

- Read any file under `Source/` and `tests/`.
- Propose changes as diffs for anything outside `Source/`.
- Ask before editing `CMakeLists.txt`, `.github/`, or any signing
  /notarization config.

Do not:

- Commit directly to `main`.
- Edit CI configuration, secrets, or installer signing material.
- Add new dependencies without asking. JUCE 8.0.6 and the standard
  library are the assumed baseline.
- Touch `LicenseManager.cpp/.h` — shared with Scope, Meter, EQT, Reflex,
  ThreeFX, Neat. Removing it breaks their builds.
- Reintroduce `ApiClient`, `ChatComponent`, `ContextPanel`, or
  `AIAnalysis` into the MixMind target. They were deliberately removed.

## Project structure

```
mixmind/
  Source/         # application code, safe to edit
  tests/          # test suite, keep in sync with Source/
  docs/           # architecture notes; update when behaviour changes
  queries/        # reviewed SQL only; no ad-hoc queries in code
```

Shared source across targets:

- `AudioAnalyzer.cpp/.h` — used by MixMind, Scope, Meter, EQT.
  Depends on `LoudnessMeter.cpp/.h`, so any target compiling
  `AudioAnalyzer` must also compile `LoudnessMeter`.
- `LicenseManager.cpp/.h` — used by Scope, Meter, EQT, Reflex,
  ThreeFX, Neat. Not used by MixMind.

When editing shared files, check every consuming target still builds.

## Code style

- Match the conventions already in the file you are editing.
- No new abstractions without a second caller.
- Comment why, not what.
- JUCE hygiene: `ScopedNoDenormals` in `processBlock`, `Atomic<float>`
  for cross-thread readouts, `SpinLock` for coefficient swaps.

## DSP rules (MixMind-specific)

- Shaper is a linear-phase FIR match-EQ. Constants are compile-time:
  `kTapCount=1024`, `kDesignOrder=11`, `kDesignSize=2048`, `kNumBins=1024`,
  `kLatency=512`. Bumping to 2048 taps / 4096 IFFT gives ~23 Hz resolution
  at ~21 ms latency — propose, don't change silently.
- 1/3-octave smoothing is the anti-pre-ringing guard. Do not remove it.
- JUCE `perform(..., inverse=true)` and `performRealOnlyInverseTransform`
  both apply 1/N. Do not add a manual 1/N scale.
- Manual mode = `traceTarget − live`. Auto mode = `ref − live`.
  Same engine serves both; do not fork it.
- **Trace v-space is linear in dB: `[0..1]` → `[−100..0]` dBFS.** A trace
  control point's value01 is the plot's dB-mapped Y, exactly as drawn.
  `buildTargetFromCurve` converts it once — `v·100 − 100`, then
  `10^(dB/20)` — because `buildMatchFilter` takes linear-magnitude bins
  and does its own `20·log10`. So `v = 0.5` is −50 dBFS (≈0.00316
  linear), **not** 0.5 linear: reading the trace as a magnitude is a
  ~44 dB error mid-axis. Canvas *bins* stay linear magnitude 0..1; only
  the trace's v-space is dB. Convert both sides to the same space before
  subtracting.
- `ReferenceAnalyzer::numBins == AudioAnalyzer::numBins == 1024`.
  If that ever changes, live-vs-target subtraction needs interpolation.
  Stop and report rather than guessing.

## Metering rules

- BS.1770 only. K-weighting shelf 1681.97 Hz Q 0.707 +3.9998 dB,
  high-pass 38.135 Hz Q 0.5003.
- Integrated uses 400 ms gating blocks, absolute gate −70 LUFS,
  relative gate −10 LU.
- True peak via 4× windowed-sinc polyphase, cutoff 0.125 cyc/sample,
  12 taps/phase.
- Never substitute RMS−3 dB for LUFS. If a source still does, fix it.
- `toLufs = -0.691 + 10·log10(energy/count)`.

## Testing

- Every behaviour change ships with a test in the same PR.
- Run the full suite before proposing a diff; do not skip failing tests.
- Never weaken an assertion to make a test pass.
- Metering and shaper changes: verify against a known reference signal
  (sine sweep for shaper, EBU R128 test tones for loudness).

## Git workflow

- Branch from `main`; one logical change per branch.
- Conventional Commits for messages.
- Never force-push a shared branch or rewrite published history.
- Do not commit `node_modules/`, `package-lock.json`, or `package.json`
  — they were removed and should stay removed.

## Current state

Read `docs/STATE.md` for current state. It is the
source of truth; this file is not. Do not enumerate progress here — it
drifts within a session.

## What this file is not

- Not a place for secrets, tokens, or connection strings.
- Not a changelog, and not documentation for humans.
- Not a wish list: every line here should change agent behaviour.
- Not a duplicate of `docs/STATE.md` — point to it, don't copy it.
