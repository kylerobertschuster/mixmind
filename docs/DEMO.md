# Demo runbook

Everything here is verifiable from the terminal. Nothing is staged.

## 1. Build

```bash
cmake -B build
cmake --build build
```

Seven plugin targets, each producing VST3 + AU + Standalone, and copying the
plugins into `~/Library/Audio/Plug-Ins/` on build:

| Target | Product | What it is |
|---|---|---|
| `MixMind` | MixMind | Reference-matching spectrum shaper — the flagship |
| `Scope` | Scope | Goniometer / phase scope |
| `Meter` | Meter | BS.1770 loudness + true-peak metering |
| `EQT` | EQT | Parametric EQ |
| `Reflex` | Reflex | — |
| `ThreeFX` | 3FX | — |
| `Neat` | Neat Neat Neat | — |

## 2. Prove the plugins are valid — Apple's own validator

This is the check a DAW actually performs on load. All seven pass:

```bash
auval -v aufx Mxmn MXMN   # MixMind
auval -v aufx Scop MXMN   # Scope
auval -v aufx Metr MXMN   # Meter
auval -v aufx Equt MXMN   # EQT
auval -v aufx Rflx MXMN   # Reflex
auval -v aufx Tfxf MXMN   # 3FX
auval -v aufx Nttt MXMN   # Neat
```

Expected: `AU VALIDATION SUCCEEDED`. List everything registered with:

```bash
auval -a | grep MXMN
```

## 3. Prove the DSP is correct — the test suite

```bash
ctest --test-dir build --output-on-failure
```

39 test cases, ~11,900 assertions, **all green**.

```bash
./build/tests/MixMind_Tests_artefacts/Release/MixMind_Tests '[loudness]'
./build/tests/MixMind_Tests_artefacts/Release/MixMind_Tests '[shaper]'
```

- `[loudness]` — 11 cases. BS.1770-4 K-weighting, 400 ms gating blocks, the
  −70/−10 LU gates, 4× true peak. Expectations are computed by an independent
  BS.1770-4 implementation evaluated in Python, not read back off the plugin —
  so the tests cannot agree with a bug in our own meter.
- `[shaper]` — 16 cases. Bypass bit-transparency, latency reporting (what the
  getter reports is what `process` applies), impulse response vs published taps,
  block-size independence, DC-normalisation, the ±24 dB clamp, and the
  trace→target map including the dB-mapped trace v-space.

Both suites compile the **shipping** `.cpp` files, not copies, so a passing test
means the code that ships passed.

## 4. Run it

```bash
./build/MixMind_artefacts/Release/Standalone/MixMind.app/Contents/MacOS/MixMind
```

All seven standalones launch and stay up. Load the VST3 or AU in any DAW.

## 5. The story worth telling

MixMind is a spectrum analyzer plus a **linear-phase FIR match-EQ**. It measures
what your mix is doing, compares it to a reference or a hand-drawn curve, and
designs a 1024-tap filter to close the gap. The match filter is designed off the
audio thread and published to it atomically; while bypassed the plugin reports
**0 samples of latency** and is bit-transparent, so it can sit in a session
without shifting anything against anything else.

The house rule is honest DSP and honest metering. Two examples:

- Loudness follows BS.1770-4 exactly — channels are **summed** with G = 1.0, not
  averaged, because averaging reads 3 dB low for correlated stereo. True peak is
  4× oversampled, not sample peak.
- The shaper's canvas trace is **dB-mapped**: v = 0.5 is −50 dBFS (≈0.00316
  linear), not 0.5 linear. `buildTargetFromCurve` converts display → dB → linear
  magnitude exactly once, because `buildMatchFilter` does its own `20·log10` on
  the bins. Reading a trace point as a magnitude was a ~44 dB error half-way up
  the axis.

## 6. The defect that was found and fixed

`tests/ShaperTests.cpp` carries a regression test:

> *outside the drawn range the trace holds its nearest endpoint*

`ShaperProcessor::buildTargetFromCurve` used to walk the curve's segments
forward only. When a bin fell **below** the first trace point, the walk ran off
the end and the fallback returned the **last** point's value — so bins below the
trace inherited the highest frequency's setting, instead of the lowest.

It is not cosmetic: `buildMatchFilter` DC-normalises by dividing the taps by
their sum, and `sum(taps) == H(0) == mag[0]`. Bin 0 is therefore the reference
the whole filter is scaled against. Measured on a trace drawn 0.0 @ 20 Hz →
1.0 @ 20 kHz over a flat spectrum, the impulse response differed by up to 8.16
per tap from the intended one.

With the default 20 Hz axis only bin 0 was affected; selecting a focus band
raises `axisMin` to the band start, and then every bin below the band inherited
the treble value.

Fixed by holding the nearest endpoint on both sides, and pinned by the test —
there is no `[!shouldfail]` tag left to remove. The assertion is stated at the
−100 dBFS floor, so it goes red again if the extrapolation ever comes back.
