# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## Build

- All 7 plugin targets + `MixMindTests` build clean (verified on Linux, GCC,
  Ninja, Debug; `-Wall`-level JUCE warning flags, no warnings in MixMind
  sources). macOS signing/notarisation is unchanged (`build_pkg.sh`).
- Formats: VST3 + AU + Standalone. AAX deferred to V2.
- JUCE 8.0.6 via `FetchContent_MakeAvailable`, C++17.
- Linux build deps (not needed on macOS): libasound2, libx11, libxrandr,
  libxinerama, libxcursor, libxcomposite, libxext, libfreetype, libfontconfig,
  libgl dev packages.

## Tests

`MixMindTests` (JUCE UnitTest, `ctest`): 178 checks, all passing.

- **Metering** — BS.1770 K-weighting coefficients vs the standard's table;
  EBU Tech 3341 cases 1–5 at 44.1 and 48 kHz (±0.1 LU); block-size
  independence; mono vs dual-mono; out-of-phase stereo; inter-sample true
  peak; crest factor; absolute gate; reset.
- **Shaper** — identity, level-neutrality, +6 dB shelf → +6 dB tilt, amount
  scaling, exact linear phase about 512, bypass = 512-sample delay, runtime
  impulse response == designed taps, stepped sine sweep (measured gain ==
  designed response ±0.05 dB), click-free swaps/toggles, trace → target,
  MANUAL end-to-end, edge hold outside the data.
- **Reference** — loudness/peak/spectrum agree with the live analyzer;
  44.1 k / 96 k files land on the 48 k grid at the same level; mono = dual-mono;
  sub-400 ms files; missing / silent / non-audio files; cancellation; formats.
- **Processor** — constant latency + latency-matched host bypass; async
  reference load; session round-trip (reference restored from the cached
  analysis with the audio file deleted, trace, parameters); pre-v2 sessions;
  failed load keeps the previous reference; AUTO and MANUAL correction end to
  end; editor open / resize / paint / close. Setting `MIXMIND_SNAPSHOT_DIR`
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
- **ShaperProcessor** — linear-phase FIR match EQ (1024 taps, latency 512,
  constant). Level-neutral, 1/3-octave smoothed, ±24 dB clamp, bins without
  data hold the neighbouring correction, sub-20 Hz untouched. Crossfaded
  filter swaps/toggles; mirrored delay line.
- **MixMindProcessor** — owns the reference, the trace and the design loop
  (message-thread timer), so closing the editor loses nothing. State v2 saves
  parameters, the trace (Hz, dB) and the reference analysis (+ path).
  Parameters carry version hints; mono and stereo layouts supported.
- **Editor / TelemetryCanvas** — parameter attachments (host automation and
  session recall reflected in the UI), drag-and-drop for audio (reference)
  and images (layer), reference menu (replace / re-analyze / reveal / clear),
  match-curve preview before SHAPE is on, LUFS/dBTP readouts with YOU − REF
  loudness difference, 1/12-octave display smoothing, dB axis labels, trace
  editing (add / drag / delete).
- **Siblings** — Meter shows real true peak / crest / band levels (it showed
  `LUFS + 3` and `LUFS × 0.2`); Scope's goniometer plots real samples (it
  plotted bass vs mid energy). LookAndFeel honours toggle colours and bold
  fonts. Dead sources removed (AnalyzerCanvas, PresetManager, HostTheme,
  TelemetryData, PipeVisualizer, JuiceBoxMeter).

## Last milestone

Sep 2026 cleanup + `honest-dsp-v1` work: MixMind compiles again (the
reference loader did not build), BS.1770-correct metering, reference
pipeline rebuilt, shaper made level-neutral / exact-latency / click-free,
test suite added. Tag `honest-dsp-v1` still pending.

## Open / next

- Decide the fate of `proxy/`: its DeepSeek `/api/chat` route is dead
  (no plugin calls it) and `/purchase` answers 402, so the site's Buy
  button currently fails. The plugins accept any `MM-` key locally.
- Sibling plugins still nag for a license on open with "N free prompts"
  wording from the AI era (LicenseManager is off-limits; the wording lives
  in each editor).
- Proposal: 2048 taps / 4096 design (≈23 Hz resolution, ≈21 ms latency) for
  tighter low-end matching — current 1024 taps can't resolve much below
  ~100 Hz.
- Logo + branding for the suite (renders in `assets/branding/`).
- Synth focus group still gated (5th group behind the default 4).
- Optional: iridescent band blending on the rainbow master.
- Browser/web marketing front door (`site/`) — after the plugin's core loop
  is airtight.
