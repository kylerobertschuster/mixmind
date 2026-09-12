---
name: mixmind-dsp
description: MixMind's DSP contract — the linear-phase FIR match-EQ (ShaperProcessor), BS.1770-4 loudness and 4x true peak (LoudnessMeter), and the 1024-bin analyzer pipeline. Load before editing, reviewing, or debugging ShaperProcessor, LoudnessMeter, AudioAnalyzer, ReferenceAnalyzer, the match-filter designer, the trace-to-target map, or any canvas that consumes FFT bins — and before changing any DSP constant.
---

# MixMind DSP contract

`AGENTS.md` holds the rules. This skill holds the *reasons* and the exact code
paths, so a change that looks locally correct does not quietly break the shaper
or the meters.

`AGENTS.md` and `docs/STATE.md` remain authoritative. If they disagree with this
file, they win — and this file gets fixed.

## Invariants

| Constant | Value | Where | Why |
|---|---|---|---|
| `kTapCount` | 1024 | `ShaperProcessor.h` | runtime FIR length |
| `kDesignOrder` | 11 | `ShaperProcessor.h` | design FFT is 2048-point |
| `kDesignSize` | `1 << 11` = 2048 | `ShaperProcessor.h` | design grid |
| `kNumBins` | `kDesignSize/2` = 1024 | `ShaperProcessor.h` | spectral bins |
| `kLatency` | `kTapCount/2` = 512 | `ShaperProcessor.h` | reported latency |
| `kDelayLen` | 2048 | `ShaperProcessor.h` | power-of-two delay line ≥ 2·kTapCount, for cheap masking |
| `fftSize` / `fftOrder` | 2048 / 11 | `AudioAnalyzer.h` | analysis FFT, Hann-windowed, time-averaged |
| `numBins` | 1024 | `AudioAnalyzer.h` | must equal `ReferenceAnalyzer::numBins` |
| `kUp` / `kPhaseTaps` | 4 / 12 | `LoudnessMeter.h` | true-peak polyphase |
| `kSegmentsPerBlock` | 4 | `LoudnessMeter.h` | 100 ms × 4 = 400 ms gating blocks, 75 % overlap |

**1024 bins is load-bearing.** `AudioAnalyzer::numBins`, `ReferenceAnalyzer::numBins`,
`TelemetryCanvas::kNumBins`, and `ShaperProcessor::kNumBins` must all agree,
because reference and live spectra are subtracted **bin for bin**. If one
changes without the others, the subtraction is silently misaligned. Stop and
report rather than guessing.

Note `EQTEditor.h` has its own unrelated `kNumBins = 512` — do not "unify" it.

Bumping to 2048 taps / 4096 IFFT gives ~23 Hz resolution at ~21 ms latency.
**Propose it, never do it silently.**

## The five traps

### 1. 1/3-octave smoothing is the anti-pre-ringing guard

`buildMatchFilter` step 2 smooths the dB difference over a multiplicative window
(`halfWin = 2^(1/6)`, prefix-sum implementation). A linear-phase FIR with sharp
magnitude transitions rings in the time domain, and pre-ringing is audible as a
smeared transient *before* the hit. Smoothing is what keeps it inaudible.

Do not remove it, reduce it, or "modernise" it into a filter-design parameter.
If it looks too coarse, measure the impulse response first.

### 2. The inverse FFT already applies 1/N

`fft.perform (spec, imp, true)` in `buildMatchFilter` step 4 normalises by 1/N
internally. Adding a manual 1/N scale makes the filter ~2048× too quiet. Same
rule for `performRealOnlyInverseTransform`.

This is why step 6 only **DC-normalises** — dividing taps by their *sum* to
preserve broadband level — and never divides by N.

### 3. The canvas trace is linear magnitude 0..1 — not dB

`TelemetryCanvas` trace points and analyzer bins are both linear magnitude 0..1.
`buildTargetFromCurve` interpolates them in *log-frequency* but *linear
magnitude*, straight against the bins.

Never convert with `v·100 − 100` (the old bug), and never convert only one side
of a subtraction. Manual mode is `traceTarget − live`; Auto is `ref − live`, both
in native units. If a dB path is ever genuinely needed, convert **both** sides
explicitly and say why in a comment.

### 4. Channels are summed, not averaged

BS.1770 sums the K-weighted mean squares of both channels with G = 1.0 —
`LoudnessMeter::process` accumulates `blockEnergy` across channels. Averaging
reads 3 dB low for correlated stereo and 6 dB low for a hard-panned source.

`toLufs = −0.691 + 10·log10(energy/count)`. Integrated uses 400 ms gating blocks
with an absolute gate at −70 LUFS and a relative gate at −10 LU. Never
substitute RMS − 3 dB for LUFS.

`process(L, nullptr, n)` is a genuinely mono source: one channel, not a
duplicated one.

### 5. One engine, no fork

Manual and Auto both reduce to "gain = target − live". `buildMatchFilter` serves
both. Do not add a second code path for either mode.

## Threading

- FIR **design** is GUI-thread only. Never design a filter on the audio thread.
- `setFilter` takes the `SpinLock`, writes the **back** buffer, then flips
  `activeIdx` — double-buffered so the audio thread never sees a half-written
  tap set.
- `process` takes the lock **once per block**, snapshots into `curTaps`, then
  runs lock-free for the rest of the block.
- Bypass is genuinely transparent: `getLatencySamples()` returns 0, and `process`
  copies in→out when disabled or when no taps are active. Hosts see 0 latency
  while bypassed — keep it that way.
- `ScopedNoDenormals` belongs in `processBlock`.
- Analyzer readouts (`fftOutput`/`fftAvg`) are plain floats written on the audio
  thread and read on the GUI thread. Torn reads are harmless for display and the
  shaper smooths 1/3-octave anyway — this is deliberate, not an oversight.
- Coefficient swaps across threads use `juce::SpinLock`. Cross-thread readouts
  use `juce::Atomic<float>`.

## File map

| File | Role |
|---|---|
| `Source/ShaperProcessor.{h,cpp}` | FIR match-EQ: `buildMatchFilter`, `buildTargetFromCurve`, lock-free `process` |
| `Source/LoudnessMeter.{h,cpp}` | BS.1770-4 LUFS (M/S/I + gates) and 4× true peak |
| `Source/AudioAnalyzer.{h,cpp}` | 2048-pt Hann FFT, linear-magnitude 0..1 bins, energy bands, stereo/phase; owns a `LoudnessMeter` |
| `Source/ReferenceAnalyzer.{h,cpp}` | loads a reference track, resamples to the live rate, emits the same 1024 bins |
| `Source/TelemetryCanvas.{h,cpp}` | dual-signal spectrum, trace drawing, reference ghost |
| `Source/AnalyzerCanvas.{h,cpp}` | standalone FFT spectrum display |
| `Source/FocusModel.h` | focus groups, band ranges, pastel/saturated colour pairs |
| `Source/PluginProcessor.{h,cpp}` | host-facing `processBlock`, latency reporting |

Shared-code warning: `AudioAnalyzer` is used by MixMind, Scope, Meter and EQT and
depends on `LoudnessMeter`, so any target compiling one must compile both.
`LoudnessMeter`/`AudioAnalyzer` changes must be checked against **every**
consuming target, not just MixMind.

## Testing

- Catch2, via `tests/CMakeLists.txt` → `add_test (NAME MixMind_Tests ...)`. Run
  the full suite before proposing a diff.
- Tests compile the **shipping** `.cpp`, not copies — a passing test means the
  code that ships passed.
- `tests/TestSignals.h` is deterministic by design: fixed-seed xorshift noise,
  integer-cycle sines. Keep it that way. A flaky DSP test is worse than none.
- Metering expectations come from an independent BS.1770-4 implementation
  evaluated in Python, never read back off the plugin. Keep that property: a
  test that asserts "whatever we currently output" pins bugs in place.
- Verify against known reference signals: sine sweep for the shaper, EBU R128
  test tones for loudness.
- Never weaken an assertion to make a test pass.

### Known gap — the shaper is untested

`tests/CMakeLists.txt` compiles **only** `LoudnessMeter.cpp`, yet its own header
comment claims coverage of "the FIR designer, the per-channel routing and its
latency, and the trace→target map".

`tests/TestSignals.h` already ships `firMagnitudeAt`, `maxAbsDiff`, `bestLag`,
`amplitudeForRmsDb`, and `noise` — **none of which are used anywhere**.
`LoudnessTests.cpp` has 11 cases, all `[loudness]`.

So `buildMatchFilter`, `buildTargetFromCurve`, and `ShaperProcessor::process`
have no runtime coverage, while the helpers meant to test them sit idle. Adding
`ShaperProcessor.cpp` to the test target is the highest-value test work in the
repo. When you touch the shaper, consider doing that first.

## Before you change any DSP

Walk [references/audit-checklist.md](references/audit-checklist.md).
