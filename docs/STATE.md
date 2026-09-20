# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## Build

- All 7 targets build clean: MixMind, Scope, Meter, EQT, Reflex, ThreeFX, Neat.
- Formats: VST3 + AU + Standalone. AAX deferred to V2.
- JUCE 8.0.6 via FetchContent, C++17.
- Catch2 suite in `tests/` (39 cases, spanning loudness, shaper, match state),
  wired into CMake via `MIXMIND_BUILD_TESTS` and run by CI. Run with
  `ctest --test-dir build --output-on-failure`.

## Code (post-cleanup, Sep 2026)

- **AI stack removed** — ApiClient, ChatComponent, ContextPanel, AIAnalysis
  are deleted from the MixMind target. No proxy/licensing scaffolding in the
  audio path.
- **ShaperProcessor** — linear-phase FIR match-EQ (kTapCount=1024, kLatency=512,
  1/3-octave smoothing). Manual = traceTarget − live; Auto = ref − live.
- **LoudnessMeter** — BS.1770-4 (K-weighting, 400 ms gating, −70/−10 LU gates,
  4× true-peak). Shared with AudioAnalyzer (Scope/Meter/EQT).
- **AudioAnalyzer / ReferenceAnalyzer** — honest LUFS; both numBins == 1024.
- **TelemetryCanvas** — trace is linear magnitude 0..1, NOT dB (the old
  `v·100 − 100` conversion is gone; subtract in native units).
- **CMakeLists** — ShaperProcessor + LoudnessMeter in MixMind; LoudnessMeter in
  Scope/Meter/EQT.

## Public surface (audited 2026-09-20)

- `BETA.md` and `site/` rewritten to describe the reference-matching shaper.
  DeepSeek/AI-chat and prompt-gated-trial copy removed. `site/buy_modal.html`
  deleted (unused, sold a checkout that could not complete).
- `site/mixmind.html` no longer links the July `.pkg`: that binary predates the
  AI removal and calls `getjuicepipe.com/api/chat`. Rebuild and repackage
  before a public download link returns. `CMakeLists.txt` says 1.0.0 while the
  old package says 4.3.0 — version story unresolved.
- `proxy`: `POST /api/chat` and the DeepSeek dependency removed; `/healthz` no
  longer reports the model, key/admin status, or license counts publicly.
  `/validate`, admin issue/revoke, and the Stripe-gated `/purchase` remain.
  `/purchase` is not routed at the live edge (nginx 404), so the old buy flow
  could never complete.
- License gate is a client-side `MM-` prefix check that blocks nothing: six
  plugins show a dismissible nag, MixMind has no LicenseManager at all.
  Decision pending — see Open / next.
- `JuicePipe_logo.jpeg` (1.7 MB, unused) untracked and ignored; the blob still
  exists in history. No git-lfs installed here.

## Last milestone

`focus-telemetry-v3` (Aug 2026): rainbow master spectrum, AI demoted to an
optional collapsed panel. The Sep 2026 cleanup (above) removes the AI panel
entirely and lands the honest shaper/metering.

## Open / next

- Rebuild + repackage MixMind (settle the version story: CMake 1.0.0 vs site
  4.3.0) before any public download returns.
- Decide the license model before quoting $9/mo or $99:
  - signed offline keys (Ed25519) for perpetual licences — no server in the
    path; or
  - short-lived signed tokens + refresh if subscriptions need revocation.
  The `MM-` prefix check is not a gate and should not be sold as one.
- Wire a real checkout (Stripe Payment Link or webhook → key email) and route
  `/purchase` at the edge.
- Logo + branding for the suite.
- Synth focus group still gated (5th group behind the default 4).
- Optional: iridescent band blending on the rainbow master.
