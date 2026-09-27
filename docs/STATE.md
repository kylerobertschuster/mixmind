# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## v1.0 scope

Locked scope for the first release. The AI features are part of v1.0.

1. **Async LLM backend queue ↔ DSP thread** — not started. The model runs
   off the audio thread; requests and results cross threads through a
   lock-free queue, and the audio thread only reads values that are already
   validated (no network, locks or allocation on the audio thread).
2. **NaN / clamping firewall in front of the lock-free audio queue** — not
   started. Every value the LLM proposes is checked for finiteness and
   clamped to its parameter's range before it is queued. The audio path's
   own non-finite guard (`runChain`) already exists and stays.
3. **OpenGL-accelerated TelemetryCanvas rendering the EQ match curves** —
   done (3909c3f): the whole editor, canvas included, renders through an
   attached `juce::OpenGLContext`. GPU frame cost still to be measured on
   real hardware.
4. **Simper SVF filters applying corrections without dropouts** — done:
   `ParametricEq` bands run as TPT (Simper) SVFs mapped exactly from the
   matched biquads, coefficients interpolated per sample, fades on discrete
   changes. The reference match itself is applied by the linear-phase FIR
   (`ShaperProcessor`), crossfaded on every filter swap.

## Build

- All 7 plugin targets + `MixMindTests` build clean (verified on Linux, GCC,
  Ninja, Debug; `-Wall`-level JUCE warning flags, no warnings in MixMind
  sources). macOS signing/notarisation is unchanged (`build_pkg.sh`).
- Formats: VST3 + AU + Standalone. AAX deferred to V2.
- JUCE 8.0.6 via `FetchContent_MakeAvailable`, C++17.
- Linux build deps (not needed on macOS): libasound2, libx11, libxrandr,
  libxinerama, libxcursor, libxcomposite, libxext, libfreetype, libfontconfig,
  libgl dev packages.

## Performance (measured, Release, one x86-64 core without AVX, 48 kHz stereo)

- Match EQ 2048 taps: 1.1 % linked, 0.7 % mid/side, 2.4 % while
  crossfading continuously (a 1024-tap direct FIR used to cost 4.5 %).
- 8 EQ bands: 0.8 % static, 5.1 % with all eight sweeping every block.
- BS.1770 meter: 0.25 %.
- Editor: on screen it renders through an attached `juce::OpenGLContext`
  (GPU); repaints stay event-driven at the canvas's 30 fps. GPU frame cost is
  not measured yet — this container only has Mesa's CPU rasteriser, so it
  needs real hardware. The software renderer (still used for snapshots and
  wherever GL is unavailable) costs ≈6.5 ms/frame at 1080×680 and ≈11.4 ms
  at 1800×1100 (Release); `MIXMIND_SNAPSHOT_DIR=… MixMindTests Processor`
  prints these and writes PNG snapshots.

## Tests

`MixMindTests` (JUCE UnitTest, `ctest`): all passing (count in the latest
commit message; Debug and Release).

- **Metering** — BS.1770 K-weighting coefficients vs the standard's table;
  EBU Tech 3341 cases 1–5 at 44.1 and 48 kHz (±0.1 LU); block-size
  independence; mono vs dual-mono; out-of-phase stereo; inter-sample true
  peak; crest factor; absolute gate; reset.
- **Shaper** — identity, level-neutrality, +6 dB shelf → +6 dB tilt, amount
  scaling, exact linear phase about 1024, bit-exact bypass delay, runtime
  impulse response == designed taps, stepped sine sweep (measured gain ==
  designed response ±0.05 dB), host-block-size independence, click-free
  swaps/toggles, mid/side filter pairs, side offset, low-end resolution,
  trace → target, MANUAL end-to-end, edge hold outside the data.
- **Equalizer** — matched design vs analog prototype for bells (±12 dB,
  Q 0.7–6, 100 Hz–16 kHz, 44.1/48/96 k), shelves, 12/24/48 dB/oct cuts,
  notch; bilinear comparison; stability of 2000 random designs; measured
  gain; Stereo/Mid/Side placement (stereo and mono); bit-exact bypass;
  click-free on/off / type / placement / frequency jumps.
- **Reference** — loudness/peak/spectrum agree with the live analyzer;
  44.1 k / 96 k files land on the 48 k grid at the same level; mono = dual-mono;
  sub-400 ms files; missing / silent / non-audio files; cancellation; formats.
- **Processor** — constant latency + latency-matched host bypass; async
  reference load; session round-trip (reference restored from the cached
  analysis with the audio file deleted, trace, parameters); pre-v2 sessions;
  failed load keeps the previous reference; AUTO, MANUAL and mid/side
  correction end to end; band parameters drive the EQ and the output
  analyzer; non-finite input contained; editor open / resize / paint / close,
  GL context attached. Setting `MIXMIND_SNAPSHOT_DIR`
  writes PNG snapshots of the editor for visual review.

## Code

- **LoudnessMeter** — BS.1770-4 / R128: per-channel K-weighting (bilinear
  forms, exact at 48 kHz), channel energies summed, 100 ms steps, 400 ms
  gating blocks with 75 % overlap, energy-domain gating via a fixed
  histogram (no audio-thread allocation), per-channel 4× true peak,
  3 s RMS/peak for crest. Thread-safe `requestReset()` (click the YOU readout).
- **AudioAnalyzer** — mid (L+R)/2 spectrum, 2048 Hann, 50 % overlap; display
  average (~200 ms) and gated long-term average (~3 s) for matching; band
  levels in dB RMS; ~300 ms width/correlation; goniometer sample ring.
- **ReferenceAnalyzer** — streams the file (WAV/AIFF/FLAC/Ogg/MP3, plus
  CoreAudio formats on macOS), analyses at the file's own rate, maps onto
  the live grid (`mapToGrid`, no resampling). Runs on a background thread.
- **ShaperProcessor** — linear-phase FIR match EQ (2048 taps, latency 1024,
  constant), uniformly partitioned FFT convolution with a direct head (zero
  added latency). Linked or mid/side filter pairs. Level-neutral, 1/3-octave
  smoothed, ±24 dB clamp, bins without data hold the neighbouring
  correction, sub-20 Hz untouched. Crossfaded swaps/toggles via a fixed slot
  pool.
- **ParametricEq** — 8 bands (bell, shelves, 12/24/48 dB/oct cuts, notch),
  analog-matched design realised as TPT SVFs with per-sample interpolation,
  Stereo/Mid/Side placement, fades on discrete changes, bit-exact when off.
  All band settings are automatable parameters.
- **MixMindProcessor** — owns the reference, the trace and the design loop
  (message-thread timer), so closing the editor loses nothing. Input and
  output analyzers; non-finite guard. State v2 saves parameters, the trace
  (Hz, dB) and the reference analysis incl. side spectrum (+ path; older
  caches without a side spectrum are refreshed from the file). Parameters
  carry version hints; mono and stereo layouts supported.
- **Editor / TelemetryCanvas** — rendered through an OpenGL context
  (`juce_opengl`, MixMind and the tests only; `paint()` runs on the GL
  thread with the message manager locked), parameter attachments (host automation and
  session recall reflected in the UI), drag-and-drop for audio (reference)
  and images (layer), reference menu (replace / re-analyze / reveal / clear),
  match-curve preview before SHAPE is on (side curve dashed in M/S),
  interactive EQ nodes (double-click add, drag, shift = fine, wheel = Q,
  right-click menu, double-click / alt-click / Delete removes; edits are
  host gestures), post-processing YOU spectrum with the input drawn faintly,
  LUFS/dBTP readouts with YOU − REF loudness difference, 1/12-octave display
  smoothing, dB axis labels, trace editing. Screenshot layer gestures need
  Shift.
- **Siblings** — Meter shows real true peak / crest / band levels (it showed
  `LUFS + 3` and `LUFS × 0.2`); Scope's goniometer plots real samples (it
  plotted bass vs mid energy). LookAndFeel honours toggle colours and bold
  fonts. Dead sources removed (AnalyzerCanvas, PresetManager, HostTheme,
  TelemetryData, PipeVisualizer, JuiceBoxMeter).

## Last milestone

Sep 2026, second pass: 2048-tap partitioned match EQ, mid/side matching,
8-band analog-matched parametric EQ with interactive nodes, output
analyzer, non-finite guard.

Sep 2026 cleanup + `honest-dsp-v1` work: MixMind compiles again (the
reference loader did not build), BS.1770-correct metering, reference
pipeline rebuilt, shaper made level-neutral / exact-latency / click-free,
test suite added. Tag `honest-dsp-v1` still pending.

## Open / next

- LLM assistant: in v1.0 scope (items 1–2 above); AGENTS.md now authorizes
  it under the real-time safety rules in its "AI integration" section.
- Decide the fate of `proxy/`: its DeepSeek `/api/chat` route is dead
  (no plugin calls it) and `/purchase` answers 402, so the site's Buy
  button currently fails. The plugins accept any `MM-` key locally.
- Sibling plugins still nag for a license on open with "N free prompts"
  wording from the AI era (LicenseManager is off-limits; the wording lives
  in each editor).
- GPU rendering: measure frame cost on real Windows / macOS hardware. On
  Windows JUCE 8 already renders through Direct2D by default and attaching
  the GL context replaces that — compare the two before release. Apple has
  deprecated OpenGL on macOS (it still works; on Apple Silicon the system
  implements it on top of Metal).
- Next resolution step would be a 4096-pt analyzer + 4096 taps (≈43 ms
  latency); the analyzer is now the limit, not the FIR.
- Other plugins (Neat, Reflex, 3FX, Scope, Meter, EQT) — next pass.
- Logo + branding for the suite (renders in `assets/branding/`).
- Synth focus group still gated (5th group behind the default 4).
- Optional: iridescent band blending on the rainbow master.
- Browser/web marketing front door (`site/`) — after the plugin's core loop
  is airtight.
