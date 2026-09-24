# DSP change audit checklist

Run this before proposing any diff that touches `ShaperProcessor`,
`LoudnessMeter`, `AudioAnalyzer`, `ReferenceAnalyzer`, or the FFT-bin path.

## 1. Scope

- [ ] Which targets compile the file being changed? (`MixMind`, `Scope`, `Meter`,
      `EQT`, `Reflex`, `ThreeFX`, `Neat`.)
- [ ] If the file is shared (`AudioAnalyzer`, `LoudnessMeter`), did all consuming
      targets build — not just MixMind?
- [ ] Does the change need a `CMakeLists.txt` edit? That requires asking first.

## 2. Bin alignment

- [ ] Does any `numBins` change? If so, do **all four** agree —
      `AudioAnalyzer`, `ReferenceAnalyzer`, `TelemetryCanvas`, `ShaperProcessor`?
- [ ] Is reference-vs-live still a same-index subtraction in native units?
- [ ] If `numBins` genuinely must change: stop and report. Live-vs-target
      subtraction then needs interpolation.

## 3. FIR design path

- [ ] Is the 1/3-octave smoothing still present and intact?
- [ ] Is there any manual `1/N` or `1/2048` scale added near the inverse FFT?
      (There must not be.)
- [ ] Is the result still DC-normalised by the tap **sum**?
- [ ] Is the filter still designed off the audio thread?
- [ ] Does the impulse response still avoid pre-ringing? If you cannot answer,
      measure it — do not assume.
- [ ] Does the tap buffer swap still happen under the `SpinLock` with the
      `activeIdx` flip *last*?

## 4. Signal-space units

- [ ] Is every operand of a subtraction in the same space (linear magnitude vs
      linear magnitude, dB vs dB)?
- [ ] Trace v-space is dB (`0..1 ↔ −100..0 dBFS`) and is converted exactly once,
      in `buildTargetFromCurve` (`v·100 − 100`, then `10^(dB/20)`); bins are
      linear magnitude 0..1 and are converted inside `buildMatchFilter`. Check
      that no path treats a trace value as a magnitude or a bin as dB.
- [ ] Are `amount`, trace values and bins all still 0..1 where documented?

## 5. Metering

- [ ] Channels summed with G = 1.0, not averaged?
- [ ] `toLufs = −0.691 + 10·log10(energy/count)` unchanged?
- [ ] Absolute gate still −70 LUFS, relative gate still −10 LU? 400 ms blocks?
- [ ] True peak still 4× oversampled (12 taps/phase), not sample peak?
- [ ] K-weighting unchanged: shelf 1681.97 Hz Q 0.707 +3.9998 dB, high-pass
      38.135 Hz Q 0.5003?
- [ ] Any `RMS − 3 dB` still absent?

## 6. Realtime safety

- [ ] No allocation, free, file I/O, or `new` in `process` / `processBlock`.
- [ ] No unbounded work whose cost depends on host block size.
- [ ] `ScopedNoDenormals` present in `processBlock`.
- [ ] Locks held for a bounded, short section — never across the whole block.
- [ ] `juce::Atomic` / `SpinLock` for anything crossing threads.

## 7. Tests

- [ ] A test shipped in the same change.
- [ ] Full suite run, not skipped.
- [ ] Any failing test investigated as a real bug, never an assertion weakened.
- [ ] Metering/shaper change verified against a known reference signal
      (sine sweep for the shaper, EBU R128 tones for loudness).
- [ ] If a number was expected, was it derived independently — or just read back
      off the plugin?

## 8. Honesty check

The house rule is *"honest DSP, honest metering, no proxy or licensing
scaffolding in the audio path."*

- [ ] Does this change make a number look better without being more correct?
- [ ] Does it approximate something that has an exact standard definition?
- [ ] Would it survive someone comparing it against a reference implementation?
