Version: 1.6
Last Reviewed: 2026-09-28
Owner: Founder

# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## v1.0 scope

Defined by ADR-006: **v1.0 ships Mix Doctor**, with ADR-007 (every finding
carries severity and confidence) and ADR-008 (mix-bus masking in v1.0,
track-aware in v1.1; offline file analysis is the first v1.x feature).
Status against the required list (details and issues in `docs/ROADMAP.md`):

- **Mix Doctor report + severity and confidence scoring** (#1) — partial:
  `MixDoctor::diagnose` turns a run's statistics (and the reference) into
  ranked findings with severity, confidence, evidence, potential causes and
  an action; six rules (true peak, tone against the reference, density, low
  end in mono, phase, loudness). In the editor: RUN MIX DOCTOR, a live
  report while it listens, kept when it stops, COPY as Markdown. Not yet:
  width against the reference, a delivery loudness target.
- **Analysis engine** (#2, #5) — done except the audio-thread allocation
  guard (#5): spectra published whole (AR-001 resolved), 100 ms measurement
  frames, and a 60 s `MeasurementHistory` of the output with BS.1770
  integrated loudness, EBU 3342 loudness range, true peak, RMS, crest,
  correlation, width, octave bands and mono fold-down.
- **Frequency masking** (#3) — not started; v1.0 = mix-bus congestion
  diagnostics with confidence and potential sources (ADR-008).
- **Reference track intelligence** (#4) — partial: native-rate mid + side
  analysis, session cache, AUTO / mid-side match exist; comparison report
  doesn't.
- **AI explanations** (#11) — partial: `AiWorker` (worker thread, lock-free FIFO)
  and `AiFirewall` (strict JSON, all-or-nothing) exist, but the firewall's
  schema covers parameter / trace suggestions only — no explanation payload,
  no model backend, no UI. AR-002 open.
- **Approval gate** — done (ADR-003): an accepted AI result changes nothing
  until `approveAiSuggestion()`.
- **Offline function** (ADR-005) — holds today: nothing needs a network.

Foundation already done (the earlier v1.0 list, replaced by ADR-006): GPU
(OpenGL) editor, Simper SVF parametric EQ, 2048-tap linear-phase match EQ,
AI worker + firewall.

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
- BS.1770 meter: 0.25 %. Whole AudioAnalyzer (meter, spectra, frames):
  0.7 % per instance (`MIXMIND_BENCH=1 MixMindTests Analysis`).
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
  sub-400 ms files; missing / silent / non-audio files; cancellation; formats;
  an implausible Result (subnormal, negative, NaN or infinite rate) never
  indexes outside its spectrum.
- **Analysis** — snapshot handoff: 200 000 values under contention, no torn
  or out-of-order read; frames tile the audio in exact 100 ms steps and are
  identical for 64 / 441 / 4096 / random block sizes; window integrated
  loudness and true peak equal the BS.1770 meter (44.1 / 48 k); loudness range
  matches EBU Tech 3342 cases 1–4 (10 / 5 / 20 / 15 LU); silence gated out of
  RMS / bands / stereo; identical, anti-phase and uncorrelated channels give
  correlation 1 / −1 / 0 and mono loss 0 / floor / −3 dB; octave bands equal
  the long-term spectrum's band means at 44.1 / 48 / 96 k; dropped frames are
  counted and never bridged; prepare() and loudness reset keep frames whole;
  the processor's history is read while its audio thread runs.
- **Diagnostics** — each rule's grades at its thresholds (true peak, tone,
  density, mono, phase, loudness); tone is level-matched (a mix 10 dB louder
  than the reference has no tonal finding); uncorrelated material is not
  called out of phase; confidence grows with the seconds heard and drops
  when frames are lost; findings are ranked, carry evidence, and the report
  is deterministic; nothing below 3 s. End to end with pink noise against
  the unaltered file as the reference: a +4 / +8 dB low shelf at 120 Hz
  reads within 0.5 dB of the value predicted from the shelf's response
  (2.82 / 5.69 dB measured, 2.82 / 5.68 predicted; Low / Medium), no other
  region above Low; the same file 6 dB down gives only a loudness note
  (−6 LU); a clipped copy reads 6.9 dB denser (High), the unclipped one
  matches the reference's crest; uncorrelated channels lose 3 dB of low
  end in mono, identical ones none, an anti-phase low end over 20 dB
  (High); through the processor, a run observes only what played after it
  began.
- **AI** — firewall: valid payloads map exactly onto parameters; NaN / ±inf
  (values and literals) reject; out-of-range quantities clamp; choices and
  switches must be exact; unknown IDs / fields, coefficients, prose, code
  fences, truncated or trailing text, malformed numbers JUCE would misread,
  deep nesting and oversize replies reject; trace rules; 9000-case fuzz
  (nothing non-finite, unknown or out of range ever accepted). Worker: 400
  results through the 8-slot FIFO in order under back-pressure; backend
  failure / exception / no backend come back as results. Processor: an
  accepted suggestion changes nothing until approved (pending, dismissed and
  superseded suggestions change nothing); once approved it applies once, as
  host gestures, and is audible (−4 dB at the band centre); rejected payloads
  change nothing; the audio thread keeps running (worst block timed) while the
  model blocks and suggestions land and are approved; closing mid-request is
  prompt.
- **Processor** — constant latency + latency-matched host bypass; async
  reference load; session round-trip (reference restored from the cached
  analysis with the audio file deleted, trace, parameters); pre-v2 sessions;
  tampered sessions (subnormal or negative rate, FFT size that doesn't match
  the rate, NaN or negative spectrum) are refused instead of installed;
  failed load keeps the previous reference; AUTO, MANUAL and mid/side
  correction end to end; band parameters drive the EQ and the output
  analyzer; non-finite input contained; editor open / resize / paint / close,
  GL context attached; Mix Doctor panel: RUN starts a run and opens the
  report, it updates while audio plays, STOP keeps it (later audio doesn't
  change it), a reopened editor shows it. Setting `MIXMIND_SNAPSHOT_DIR`
  writes PNG snapshots of the editor for visual review.

## Code

- **LoudnessMeter** — BS.1770-4 / R128: per-channel K-weighting (bilinear
  forms, exact at 48 kHz), channel energies summed, 100 ms steps, 400 ms
  gating blocks with 75 % overlap, energy-domain gating via a fixed
  histogram (no audio-thread allocation), per-channel 4× true peak,
  3 s RMS/peak for crest. Thread-safe `requestReset()` (click the YOU readout).
- **AudioAnalyzer** — mid (L+R)/2 spectrum, 2048 Hann, 50 % overlap; display
  average (~200 ms) and gated long-term average (~3 s) for matching, published
  whole through a `SnapshotBuffer` (`getSpectra()`, message thread, once per
  callback); a `MeasurementFrame` per 100 ms loudness step through a lock-free
  FIFO; band levels in dB RMS; ~300 ms width/correlation; goniometer ring.
- **MeasurementHistory** — the output's last 60 s of frames (drained by the
  design loop); window statistics per BS.1770-4 and EBU Tech 3342 plus true
  peak, RMS, crest, stereo, octave bands and mono fold-down; blocks never span
  a dropped frame.
- **MixDoctor** — the diagnostic engine (message thread, on request): rules
  with explicit thresholds over `MeasurementHistory` statistics and a
  `ReferenceProfile` of the reference on the live grid; a run
  (`beginMixDoctorRun()`) observes the output from its start, otherwise the
  last 20 s. Thresholds are explicit and tested, not yet calibrated against
  labelled mixes, so confidence stays High / Medium / Low (ADR-007).
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
- **AiFirewall / AiWorker** — strict JSON check + all-or-nothing firewall
  (ADR-004) and the worker's lock-free result FIFO; accepted results wait for
  approval (ADR-003). The worker thread starts on first request; results
  are drained by the design-loop timer.
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
  Shift. Mix Doctor bar along the bottom (RUN / STOP, progress, headline,
  COPY) and the report docked beside the plot (`MixDoctorPanel`).
- **Siblings** — Meter shows real true peak / crest / band levels (it showed
  `LUFS + 3` and `LUFS × 0.2`); Scope's goniometer plots real samples (it
  plotted bass vs mid energy). LookAndFeel honours toggle colours and bold
  fonts. Dead sources removed (AnalyzerCanvas, PresetManager, HostTheme,
  TelemetryData, PipeVisualizer, JuiceBoxMeter).

## Last milestone

Sep 2026, docs and AI: project documents (VISION, ARCHITECTURE with
current vs target state, ROADMAP, FOUNDERS_NOTES, ADR-001…005) with an
authority order in AGENTS.md; AI worker + strict firewall; AI suggestions
require user approval; OpenGL editor. Roadmap issues #1–#10 opened.

Sep 2026, second pass: 2048-tap partitioned match EQ, mid/side matching,
8-band analog-matched parametric EQ with interactive nodes, output
analyzer, non-finite guard.

Sep 2026 cleanup + `honest-dsp-v1` work: MixMind compiles again (the
reference loader did not build), BS.1770-correct metering, reference
pipeline rebuilt, shaper made level-neutral / exact-latency / click-free,
test suite added. Tag `honest-dsp-v1` still pending.

## Open / next

- Build order toward ADR-006: ~~snapshots / rolling window (#2)~~ done →
  ~~first Mix Doctor rules and report panel (#1)~~ done → the comparison
  report (#4) → mix-bus congestion diagnostics (#3) → AI explanations
  (#11); the allocation guard (#5) alongside.
- Fonts: the UI names "Helvetica Neue", which only macOS ships. The report
  falls back to a common sans elsewhere; the rest of the editor relies on
  JUCE's fallback. Pick a face per platform (or bundle one) before a
  Windows release.
- AI: pick the v1.0 model provider (local Ollama / cloud API / the `proxy/`)
  in #11; its calls must be bounded and cancellable (AR-002).
- Launch page (`site/mixmind.html`) rewritten for v1.0 on this branch; it
  describes features that don't exist yet, so it must not be published
  before v1.0 ships. The trial link points at `MixMind-1.0.0.pkg`, which
  doesn't exist yet, and the Buy flow fails until #9 (`/purchase` → 402).
- Low-end resolution: a 4096-pt (or multi-resolution) analyzer is likely
  needed for low-end congestion diagnostics (#3, ADR-008); changing it also changes `mapToGrid` and the
  match-EQ grid — design note first.
- GPU rendering: measure frame cost on real Windows / macOS hardware. On
  Windows JUCE 8 already renders through Direct2D by default and attaching
  the GL context replaces that — compare the two before release. Apple has
  deprecated OpenGL on macOS (it still works; on Apple Silicon the system
  implements it on top of Metal).
- Release work (#9, #10): the `proxy/` `/purchase` route answers 402 so the
  site's Buy button fails; plugins accept any `MM-` key; sibling plugins
  still show AI-era "free prompts" wording (LicenseManager is off-limits; the
  wording lives in each editor).
- Sibling plugins (Neat, Reflex, 3FX, Scope, Meter, EQT) are separate
  products, not part of MixMind v1.0; their pass is still pending.
- Parked, outside the v1.0 scope (kept so nothing is lost): suite logo and
  branding (`assets/branding/`); synth focus group (5th group behind the
  default 4); iridescent band blending on the rainbow master; the `site/`
  front door.
