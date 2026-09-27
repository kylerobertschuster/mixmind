Version: 1.1
Last Reviewed: 2026-09-27
Owner: Founder

# AGENTS.md

Operating instructions for the Pi coding agent working in `mixmind`.
Keep this file at the repository root. It is the only agent file — do not
add an `AGENT.md`.

## Purpose

MixMind is a JUCE-based, understanding-first mix diagnostic plugin, with a
reference-matching shaper and a parametric EQ as the tools that act on its
findings. The flagship goal is FabFilter / Ozone 8 caliber: honest DSP,
honest metering, no proxy or licensing scaffolding in the audio path.
Prefer correctness over cleverness. When a request is ambiguous, ask —
do not choose a definition and proceed.

Mission, principles and product identity live in `docs/VISION.md`:
understanding first, diagnosis as the primary value, processing tools that
act on findings, the producer always in control.

## Documents and authority

Authority, highest first: `docs/VISION.md` → `docs/ARCHITECTURE.md` →
`AGENTS.md` → `docs/STATE.md`. `docs/ADR/` records the decisions behind them;
`docs/ROADMAP.md` orders the work; `docs/FOUNDERS_NOTES.md` keeps the
reasoning.

- If documents conflict — with each other or with the code — raise the
  disagreement with the user. Do not silently resolve it.
- Every doc starts with `Version`, `Last Reviewed`, `Owner`. Bump the version
  on a substantive change. Git is the history: no copied `-v1` files.
- `VISION.md`: clarifications and wording only; mission, audience or product
  category changes need founder review.
- `ARCHITECTURE.md`: implementation detail follows the code; changes to
  threading, networking, latency or the DSP pipeline need review. Keep
  "Current State" true to the code; in "Target State" mark every component
  PLANNED / PARTIAL with its issue. Never describe a planned component as if
  it exists.
- `FOUNDERS_NOTES.md` and ADRs are append-only: add dated entries; supersede
  an ADR with a new one rather than editing its decision. A change that
  contradicts an Accepted ADR needs a new ADR first.

## Commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # configure (JUCE 8.0.6, C++17)
cmake --build build                                  # build all targets + tests
ctest --test-dir build --output-on-failure           # run the test suite
cmake --build build --target MixMind_Standalone
./build/MixMind_artefacts/Release/Standalone/MixMind  # run standalone (MixMind.app on macOS)
```

Targets: `MixMind`, `Scope`, `Meter`, `EQT`, `Reflex`, `ThreeFX`, `Neat`, plus
`MixMindTests` (JUCE `UnitTest` console app; `-DMIXMIND_BUILD_TESTS=OFF` skips
it). `MixMindTests <category>` runs one category: `Metering`, `Shaper`,
`Equalizer`, `Reference`, `Processor`, `AI`. On Linux the processor tests paint the editor, so run
them under `xvfb-run -a` when there is no display.
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

## Project structure

```
mixmind/
  Source/         # application code, safe to edit
  tests/          # test suite, keep in sync with Source/
  docs/           # VISION, ARCHITECTURE, ROADMAP, STATE, FOUNDERS_NOTES, ADR/;
                  # update when behaviour changes
  assets/         # logos / icons (branding renders in assets/branding/)
  site/           # static marketing site
  proxy/          # Node backend the site's checkout calls (not used by any plugin)
```

Shared source across targets:

- `AudioAnalyzer.cpp/.h` — used by MixMind, Scope, Meter, EQT.
  Depends on `LoudnessMeter.cpp/.h`, so any target compiling
  `AudioAnalyzer` must also compile `LoudnessMeter`.
- `LookAndFeel.cpp/.h` — used by every target.
- `ShaperProcessor`, `ParametricEq`, `ReferenceAnalyzer`, `TelemetryCanvas`,
  `AiFirewall`, `AiWorker` — MixMind (and `MixMindTests`) only.
- `LicenseManager.cpp/.h` — used by Scope, Meter, EQT, Reflex,
  ThreeFX, Neat. Not used by MixMind.

When editing shared files, check every consuming target still builds.

## Code style

- Match the conventions already in the file you are editing.
- No new abstractions without a second caller.
- Comment why, not what.
- JUCE hygiene: `ScopedNoDenormals` in `processBlock`, `Atomic<float>`
  for cross-thread readouts, `SpinLock` for coefficient swaps.
- The MixMind editor renders through an attached `OpenGLContext`, so
  `paint()` runs on the GL thread with the message manager locked: no
  blocking work, no message-thread-only calls, no waiting on the message
  thread from a paint.

## AI integration (v1.0)

Governed by ADR-002 (DSP is the source of truth), ADR-003 (AI
recommendations require user approval), ADR-004 (strict JSON firewall) and
ADR-005 (offline diagnostics must function).

AI/LLM integration is part of the v1.0 scope. Its job is to explain findings
and prepare actions. It never measures, never creates findings, and never
changes the audio on its own. Nothing may depend on it: with no model and no
network, every measurement and diagnostic still works. `ApiClient`,
`ChatComponent`, `ContextPanel` and `AIAnalysis` may return to the MixMind
target when they meet these rules:

- All network and LLM calls run entirely off the audio thread, on an
  asynchronous worker. The audio thread never waits on, allocates for, or
  calls into the AI path.
- Communication between the worker thread and the DSP thread uses lock-free
  ring buffers (FIFO) — no locks, no blocking handoffs.
- Every coefficient, curve or parameter payload coming from the AI passes a
  strict NaN / inf / clamping firewall before it touches the lock-free audio
  queue. Nothing from the AI bypasses it.
- Every AI-prepared change is a suggestion until the user approves it. Never
  apply an AI payload without an explicit user action; any "auto" behaviour
  needs a new ADR.
- How that is built: backends run only on the `AiWorker` thread (poll
  `shouldCancel`, use timeouts); `AiFirewall` validates on that thread and the
  result goes through the worker's lock-free SPSC FIFO as plain data. The FIFO
  is drained on the message thread (`MixMindProcessor::drainAi`), which keeps
  the latest accepted payload as the pending suggestion;
  `approveAiSuggestion()` applies it as host-visible parameter gestures and a
  trace edit, and the DSP picks them up through the parameters' atomics and
  the FIR slot pool. Never apply AI changes from the audio thread — parameter
  notifications there post messages and can block.
- The firewall is all-or-nothing (one bad field rejects the payload) and only
  clamps continuous quantities; choices and switches must be exact. The AI
  never supplies filter coefficients — it proposes band settings and the
  analog-matched designer computes them. Model text passes the strict JSON
  check before `juce::JSON` sees it: JUCE's parser misreads malformed numbers
  (`-x` → −72) and recurses without a depth limit.

## DSP rules (MixMind-specific)

- Signal path: input analyzer → shaper (match FIR) → `ParametricEq` bands →
  non-finite guard → output analyzer. The match is designed from the input
  analyzer; YOU curves/readouts show the output analyzer.
- Reference-match correction runs on the linear-phase FIR (`ShaperProcessor`);
  the 8-band parametric EQ runs on Simper (TPT) SVFs (`ParametricEq`). Neither
  moves onto the other's engine — AI-driven changes included.
- Shaper is a linear-phase FIR match-EQ. Constants are compile-time:
  `kTapCount=2048`, `kDesignOrder=12`, `kDesignSize=4096`, `kNumBins=2048`,
  `kLatency=1024` (≈21 ms @ 48 k). The analyzer's 2048-pt Hann already limits
  resolution to the same ≈47 Hz, so more taps also need a bigger analyzer
  FFT — propose, don't change silently.
- Convolution is uniformly partitioned (`kPartition=128`): the first
  partition runs direct (no added latency), the rest via FFT once per
  partition. Output must not depend on the host block size (tested).
- Taps are exactly symmetric about `kLatency` (group delay = 1024, matching
  the reported latency). Latency is constant: SHAPE off / host bypass is a
  pure `kLatency` delay, never 0, and bit-exact. Filter swaps and toggles
  crossfade over `kFadeSamples`; filters are handed over through a fixed slot
  pool and the audio thread only try-locks the `SpinLock` (no allocation on
  the audio thread).
- The match is level-neutral: the octave-weighted mean of `target − live`
  (40 Hz–16 kHz) is removed before design. Do not reintroduce DC
  normalisation — it turned a DC-bin cut into a broadband boost.
- 1/3-octave smoothing is the anti-pre-ringing guard. Do not remove it.
- JUCE `perform(..., inverse=true)` and `performRealOnlyInverseTransform`
  both apply 1/N. Do not add a manual 1/N scale.
- Manual mode = `traceTarget − live`. Auto mode = `ref − live`.
  Same engine serves both; do not fork it.
- Mid/side match (`matchStereo`, AUTO only): mid = `refMid − liveMid`, side =
  `refSide − liveSide`, and the side removes the *mid's* level offset (not its
  own), so width per band follows the reference while loudness does not
  move. A side with nothing to compare (mono mix) gets no correction. MANUAL
  is always linked.
- **The trace is stored as (Hz, dB), never as pixels or 0..1 plot units.**
  The canvas y-axis is linear in dB (`TelemetryCanvas::dbToUnit/unitToDb`,
  −100..0 dB, 0 dB = full-scale sine on the analyzer scale), so a plot
  height is *not* a linear magnitude. `ShaperProcessor::buildTargetFromCurve`
  is the one place dB → linear happens. (Reading plot height as linear
  magnitude makes MANUAL mode boost by up to the 24 dB clamp.)
- The auto-match compares against `AudioAnalyzer::getLongTermBins()` /
  `getLongTermSideBins()` (≈3 s, gated at −70 dBFS on the mid), not the
  ~200 ms display average.
- `ReferenceAnalyzer` analyses at the file's own rate and
  `ReferenceAnalyzer::mapToGrid` produces `AudioAnalyzer::numBins` (1024)
  bins on the live grid. Live and reference both analyse the mid signal
  (L+R)/2. If the live FFT size ever changes, `mapToGrid` and the shaper's
  design grid must change with it — stop and report rather than guessing.

## EQ rules (`ParametricEq`)

- Sections are analog-matched, never bilinear (RBJ) — bilinear bells cramp
  toward Nyquist. Poles: impulse-invariant image of the analog poles. Zeros:
  exact at DC and the band frequency, Nyquist term by least squares over
  f0/8 … 0.45·fs. Cut bells, boosting high shelves and cutting low shelves
  are designed as their inverse and inverted (keeps poles below f0 and the
  shape in the poles).
- Sections run as TPT SVFs (g, k, mL/mB/mH mapped exactly from the matched
  biquad — do not derive g from `tan(πf/fs)`, that re-introduces bilinear
  poles), coefficients interpolated per sample. Discrete changes (type,
  slope, placement, on/off) fade the band out and in.
- All bands off = bit-exact pass-through. Placement: Stereo / Mid / Side.
- Accuracy is tested against the analog prototype (≤ 0.75 dB to 0.45·fs,
  ignoring points below −60 dB). Don't loosen that; improve the design.

## Metering rules

- BS.1770 only. K-weighting shelf 1681.97 Hz Q 0.707 +3.9998 dB,
  high-pass 38.135 Hz Q 0.5003.
- K-weighting per channel; channel energies are summed (L, R weight 1).
  A mono programme (`R == nullptr`) is one channel. Never measure the mono
  sum (L+R)/2 — it reads 3 dB low.
- Integrated uses 400 ms gating blocks with 75 % overlap, absolute gate
  −70 LUFS, relative gate −10 LU, gated on block *energies* (not by
  averaging LUFS values).
- True peak via 4× windowed-sinc polyphase, cutoff 0.125 cyc/sample,
  12 taps/phase, per channel (max over channels).
- Never substitute RMS−3 dB for LUFS, `LUFS + 3` for true peak, or any
  other proxy. If a source still does, fix it.
- `toLufs = -0.691 + 10·log10(energy/count)`, energy summed over channels.
- No non-finite sample reaches an analyzer or the host: `runChain` zeroes
  non-finite input and, if the output is ever non-finite, silences the block
  and resets the filters. Never add a hard clipper or DC blocker "for
  safety" — they change the sound.

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
- Do not commit `node_modules/`, `package-lock.json`, or a root
  `package.json` — they were removed and should stay removed.
  (`proxy/package.json` belongs to the site backend.)

## Current state

Read `docs/STATE.md` for current state. It is the
source of truth; this file is not. Do not enumerate progress here — it
drifts within a session.

## What this file is not

- Not a place for secrets, tokens, or connection strings.
- Not a changelog, and not documentation for humans.
- Not a wish list: every line here should change agent behaviour.
- Not a duplicate of `docs/STATE.md` — point to it, don't copy it.
