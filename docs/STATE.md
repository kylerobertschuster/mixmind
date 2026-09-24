# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## Build

- All 7 targets build clean: MixMind, Scope, Meter, EQT, Reflex, ThreeFX, Neat.
- Formats: VST3 + AU + Standalone. AAX deferred to V2.
- JUCE 8.0.6 via FetchContent, C++17.

## Code (post-cleanup, Sep 2026)

- **AI stack removed** — ApiClient, ChatComponent, ContextPanel, AIAnalysis
  are deleted from the MixMind target. No proxy/licensing scaffolding in the
  audio path.
- **ShaperProcessor** — linear-phase FIR match-EQ (kTapCount=1024, kLatency=512,
  1/3-octave smoothing). Manual = traceTarget − live; Auto = ref − live.
- **LoudnessMeter** — BS.1770-4 (K-weighting, 400 ms gating, −70/−10 LU gates,
  4× true-peak). Shared with AudioAnalyzer (Scope/Meter/EQT).
- **AudioAnalyzer / ReferenceAnalyzer** — honest LUFS; both numBins == 1024.
- **TelemetryCanvas** — trace v-space is linear in dB (0..1 ↔ −100..0 dBFS);
  `buildTargetFromCurve` converts display → dB → linear magnitude, which
  `buildMatchFilter` then subtracts in dB against the live bins.
- **CMakeLists** — ShaperProcessor + LoudnessMeter in MixMind; LoudnessMeter in
  Scope/Meter/EQT.

## Last milestone

`focus-telemetry-v3` (Aug 2026): rainbow master spectrum, AI demoted to an
optional collapsed panel. The Sep 2026 cleanup (above) removes the AI panel
entirely and lands the honest shaper/metering — tag `honest-dsp-v1` pending.

## Open / next

- Logo + branding for the suite.
- Synth focus group still gated (5th group behind the default 4).
- Optional: iridescent band blending on the rainbow master.
- Browser/web marketing front door (`site/`) — after the plugin's core loop
  is airtight.
