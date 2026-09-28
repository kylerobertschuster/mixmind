Version: 1.4
Last Reviewed: 2026-09-28
Owner: Founder

# MixMind Architecture

How MixMind measures, diagnoses, explains and presents audio information
while staying real-time safe.

Two parts, never mixed: **Current State** is what the code does today and
must stay true to it. **Target State** is where it is going; every component
there is marked PLANNED or PARTIAL with its issue. Changes to threading,
networking, latency or the DSP pipeline need review (AGENTS.md, "Documents
and authority").

Governing decisions: [ADR-001](ADR/ADR-001-mix-doctor-is-primary-product-surface.md) ·
[ADR-002](ADR/ADR-002-dsp-is-source-of-truth.md) ·
[ADR-003](ADR/ADR-003-ai-recommendations-require-user-approval.md) ·
[ADR-004](ADR/ADR-004-strict-json-firewall.md) ·
[ADR-005](ADR/ADR-005-offline-diagnostics-must-function.md) ·
[ADR-006](ADR/ADR-006-v1-is-mix-doctor.md) ·
[ADR-007](ADR/ADR-007-diagnostic-confidence.md) ·
[ADR-008](ADR/ADR-008-v1-masking-is-mix-bus.md)

---

# Known Architectural Risks

Listed first because they qualify everything below. Each risk keeps its ID
forever; when fixed it is marked Resolved with the change that fixed it,
never deleted. A change that touches an open risk says so and updates it.

## AR-001 — Analyzer spectra cross threads without synchronisation

Status: **Resolved** 2026-09-28 by the analysis engine
([#2](https://github.com/kylerobertschuster/mixmind/issues/2)) · Priority: High
· Kept closed by [#5](https://github.com/kylerobertschuster/mixmind/issues/5)

**Description.** `AudioAnalyzer` writes its display and long-term spectra
(plain `float` arrays) on the audio thread. The design loop and the editor
(message thread) and the canvas `paint()` (GL thread) read them with no
synchronisation. Scalar readouts are atomics; the spectra are not.

**Potential effects**
- Torn reads: one read mixes two analysis updates. Today the display
  smoothing and the match design's averaging absorb them.
- UI inconsistency: curves drawn in the same frame can come from different
  updates.
- Undefined behaviour: formally a data race. The compiler is allowed to
  break it, and ThreadSanitizer would flag it.

**Why it matters more now.** Mix Doctor findings will be computed from these
spectra, and a finding has to come from one consistent measurement (ADR-002).

**Target.** The audio thread publishes complete, fixed-size snapshots
(lock-free FIFO, or a double buffer with an atomic index); readers only ever
see whole snapshots.

**Resolution.** `AudioAnalyzer` now publishes its spectra as one `Spectra`
set through a `SnapshotBuffer` (triple buffer: wait-free, the writer never
touches the slot the reader holds). Readers call `getSpectra()` once per
callback on the message thread and use that set; the canvas copies what it
needs, so `paint()` never reads analyzer memory. Measurement frames go through
a lock-free SPSC FIFO. Tested: 200 000 snapshots handed over under contention
with no torn or out-of-order read, and a live processor read while its audio
thread runs.

## AR-002 — AI worker shutdown depends on cooperative cancellation

Status: **Open** · Priority: **Medium** (High once a real model backend
lands) · Target resolution: the backend contract, tested against the first
real backend

**Description.** `~AiWorker` asks its thread to exit and waits 4 s. A backend
that ignores `shouldCancel` — e.g. a blocking HTTP call with no timeout —
outlives that, and JUCE then kills the thread by force, which can leave locks
or the heap in a broken state and crash the host when the plugin closes.

**Target.** Every backend uses bounded timeouts and polls `shouldCancel`
(streaming or progress callbacks), and a test closes the plugin mid-request
against the real backend. The fake-backend case is already tested.

---

# Current State

## Signal path (audio thread, `MixMindProcessor::runChain`)

```
input ─► non-finite → 0 ─► input AudioAnalyzer ─► ShaperProcessor ─► ParametricEq ─► non-finite guard ─► output AudioAnalyzer ─► host
                           (match source)        (linear-phase FIR)  (8 × TPT SVF)    (silence + reset)   (YOU curve, readouts)
```

- **Input analyzer**: mid (L+R)/2 spectrum, 2048-point Hann, 50 % overlap;
  ~200 ms display average; ~3 s long-term average gated at −70 dBFS (mid and
  side) used for matching; BS.1770-4 loudness per channel, 4× true peak,
  crest; ~300 ms width / correlation; goniometer ring. Spectra are published
  whole (`SnapshotBuffer`); every 100 ms loudness step also yields a
  `MeasurementFrame` (below). The output analyzer is the same class.
- **Match EQ** (`ShaperProcessor`): 2048-tap linear-phase FIR, uniformly
  partitioned convolution (128-sample direct head, FFT tail), linked or
  mid/side filter pairs. Latency is constant at 1024 samples; SHAPE off and
  host bypass are a bit-exact 1024-sample delay.
- **Parametric EQ** (`ParametricEq`): 8 bands, analog-matched designs run as
  TPT (Simper) SVFs with per-sample coefficient interpolation; Stereo / Mid /
  Side placement; bit-exact when all bands are off.
- **Non-finite guard**: non-finite input becomes silence; a non-finite output
  block is silenced and the filters reset. No clipper, no DC blocker.

## Threads and ownership

| Thread | Runs | Writes | Reads |
|---|---|---|---|
| **Audio** (host) | `processBlock` → `runChain` | analyzer spectra and readouts, FIR convolution state, EQ voices | parameters, FIR slot pool, band settings |
| **Message** | design-loop timer (30 Hz): `drainAi`, drains the output analyzer's frames into `MeasurementHistory`, match design (`buildCorrection` → `designFromCurve` → `setFilter`); reference install; applying approved AI suggestions; Mix Doctor diagnosis on request; editor timer (30 Hz) | trace, correction curves, AI status and pending suggestion, parameters (as host gestures) | long-term spectra, reference |
| **GL** | editor and canvas `paint()` with the message manager locked | — | what the message thread reads |
| **Reference loader** (`ThreadPool`, 1 thread) | `ReferenceAnalyzer::analyse` on a file at its own rate | result, handed to the message thread with `callAsync` | the audio file |
| **AI worker** (`AiWorker`, started on first request) | model call → strict JSON check → `AiFirewall` → FIFO | its FIFO slots | request list |
| **Host** (any) | `getStateInformation` / `setStateInformation` | state | reference under `stateLock` |

## Cross-thread handoffs

| Data | From → to | Mechanism |
|---|---|---|
| Parameters | message / host → audio | APVTS `std::atomic<float>` |
| Match filters | message → audio | 5-slot pool; writer takes a `SpinLock`, the audio thread only try-locks; swaps crossfade over 2048 samples |
| Scalar readouts (LUFS, peaks, width, …) | audio → message / GL | `juce::Atomic<float>` |
| Spectra (display, long-term mid and side) | audio → message | `SnapshotBuffer` triple buffer, one reader thread; take `getSpectra()` once per callback (AR-001, resolved) |
| Measurement frames (100 ms) | audio → message | lock-free SPSC FIFO (`AbstractFifo`, 256 frames ≈ 25 s); never waited for — a frame nobody reads is dropped and its index goes missing |
| Reference analysis | loader → message; host threads | `shared_ptr<const Result>` under a `CriticalSection`; never touched by the audio thread |
| AI requests | message → AI worker | list under a `CriticalSection`; never touched by the audio thread |
| AI results | AI worker → message | lock-free SPSC FIFO (`AbstractFifo`) of fixed-size plain-data records |
| Loudness reset | message → audio | atomic flag |

## AI path (current implementation)

```
submitAiRequest ─► AI worker: backend (blocking, cancellable) ─► strict JSON ─► AiFirewall ─► SPSC FIFO
                                                                                               │
message thread: drainAi ─► pending suggestion ─► approveAiSuggestion (user) ─► parameter gestures + trace ─► DSP (atomics, slot pool)
```

- Backends run only on the worker thread; exceptions are contained; results
  (accepted, rejected with a reason, or failed) always come back through the
  FIFO. Shutdown relies on the backend honouring `shouldCancel` — **AR-002**.
- `AiFirewall` is all-or-nothing, clamps only continuous quantities, and
  requires exact choices and switches. The AI never supplies filter
  coefficients (ADR-004).
- An accepted result is a **suggestion**; only the latest one is kept, and
  nothing changes until `approveAiSuggestion()` (ADR-003). Approval applies it
  as host-visible, undoable parameter gestures.
- **No model backend and no UI exist yet**; `setAiBackend()` is the hook.

## Analysis history (the window Mix Doctor observes)

Each 100 ms loudness step closes a `MeasurementFrame`: K-weighted energy,
unweighted energy, true peak, L/R/cross sums, and — averaged over the FFT
frames in the step — mid magnitude, mid power and side power per octave band
(31.25 Hz … 16 kHz, on the analyzer grid). The host block is cut at step ends
so frames tile the audio exactly whatever the block size. `MeasurementHistory`
(message thread) keeps the last 60 s of the output analyzer's frames and
computes, over any stretch: BS.1770-4 integrated loudness, EBU Tech 3342
loudness range, max true peak, and over frames with signal (−70 dBFS) RMS,
crest, correlation, width, octave-band levels and mono fold-down loss. Blocks
never span a missing frame; a new `prepare()` starts a new history. Cost:
0.7 % of one core per analyzer (Release, 48 kHz stereo).

## Diagnostic engine (Mix Doctor rules)

`MixDoctor::diagnose` (message thread, on request) turns the statistics of a
stretch of the output's history, and a `ReferenceProfile` of the loaded
reference on the live grid, into findings. It never measures and is
deterministic: the same statistics give the same report. A run
(`beginMixDoctorRun()`) observes the output from the moment it starts, up to
the 60 s history; without a run the report covers the last 20 s. Below 3 s of
signal it reports nothing.

| Rule | Compared with | Severity | Confidence |
|---|---|---|---|
| True peak | −1 dBTP (EBU R 128) | above 0 dBTP High, above −1 Medium | High when over (a measured maximum), else by duration |
| Tone, 5 regions (22 Hz–22 kHz) | reference, level-matched (the mean difference across regions is removed) | \|dev\| ≥ 2 dB Low, ≥ 3.5 Medium, ≥ 6 High | by duration |
| Density (crest factor) | reference | ≤ −3 dB Medium, ≤ −6 High; ≥ +6 Low | by duration, at most Medium (different stretches) |
| Low end in mono (22–177 Hz) | the mix's own mono fold-down | loss ≤ −1 dB Low, ≤ −3 Medium, ≤ −6 High | Medium from 3 s, High from 10 s |
| Phase | correlation below 0 | High | as mono |
| Loudness | reference | ≥ 3 LU Low | Medium |

Confidence by duration is Medium from 8 s of signal and High from 20 s; more
than 10 % of the window's frames lost lowers it one level (ADR-007).
Inferred findings list potential causes, never tracks (ADR-008). A report
renders as Markdown (Critical / Moderate / Healthy); it has no UI yet.

## Real-time rules (enforced today by review and tests)

The audio thread may read and analyse audio, run the DSP chain, update
atomics and try-lock the filter pool. It may not allocate, wait on a lock,
touch the network or the filesystem, format text or JSON, or call into the
AI path. Violations are bugs. An automated allocation guard is planned (#5).

## State and latency

Reported latency is constant (1024 samples). Session state (v2) holds the
parameters, the trace in (Hz, dB), and the reference analysis including its
side spectrum, so a session reopens with its reference even if the file
moved. Pending AI suggestions are not saved.

## Editor

Renders through an attached `OpenGLContext`; repaints are event-driven at
30 fps. The canvas caches its grid and curve paths.

---

# Target State

## Layers

```
Audio thread ─► Analysis engine ─────► Diagnostic engine ─────► UI
 (measure)      (history, snapshots)   (Mix Doctor: findings,   (report, evidence,
                                        severity, evidence)       actions)
                                              │                     ▲
                                              ▼                     │
                                        AI layer (optional) ────────┘
                                        explains findings, prepares actions
                                              │ user approves
                                              ▼
                                        AiFirewall ─► parameters ─► DSP
```

The AI is a side branch, not a stage in the main path: the report works with
no model and no network (ADR-005). New layers may diagnose or explain; none
may measure outside the analysis engine, create findings outside the
diagnostic engine, or change the audio without the user (ADR-002, ADR-003).

## Components

| Component | Status | Issue | Notes |
|---|---|---|---|
| Audio-thread allocation guard | PLANNED | [#5](https://github.com/kylerobertschuster/mixmind/issues/5) | proves `processBlock` never allocates; keeps AR-001 closed |
| Session trends beyond 60 s | PLANNED | [#2](https://github.com/kylerobertschuster/mixmind/issues/2) | v1.x nice-to-have (ADR-006); the 60 s history exists |
| Diagnostic engine / Mix Doctor (`Finding`: observation, impact, severity, confidence, evidence, potential causes, actions) | PARTIAL | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) | the engine and six rules exist (Current State); the report panel doesn't; congestion rules come with #3; thresholds are explicit and tested but not calibrated against labelled mixes |
| Reference intelligence (comparison report, several references, audition) | PARTIAL | [#4](https://github.com/kylerobertschuster/mixmind/issues/4) | analysis, caching and match EQ exist |
| Congestion diagnostics (mix bus, v1.0) | PLANNED | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) | low-end / midrange congestion, transient suppression, spectral crowding; potential sources with confidence, never named tracks (ADR-008) |
| Track-aware masking (v1.1) | PLANNED | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) | instance discovery, cross-instance spectral exchange, per-source masking (ADR-008) |
| Multi-resolution analysis for the low end | PROPOSED | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) | changing the live FFT also changes `mapToGrid` and the match-EQ grid — design note first |
| Translation predictor | PLANNED | [#6](https://github.com/kylerobertschuster/mixmind/issues/6) | defined scores, relative to the reference until calibrated |
| Section detector | PLANNED | [#7](https://github.com/kylerobertschuster/mixmind/issues/7) | offline on a bounce first; real-time with lag |
| Offline file analysis | PLANNED | [#8](https://github.com/kylerobertschuster/mixmind/issues/8) | same engines as real time, on a background thread |
| AI explanation layer (explanation payload, backend, UI) | PARTIAL | [#11](https://github.com/kylerobertschuster/mixmind/issues/11) | worker, firewall and approval exist; the explanation schema (AI text only, keyed by finding ID; numbers checked against evidence) is specified in #11, not built; provider undecided; AR-002 |
