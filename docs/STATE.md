# STATE — MixMind

Current-state snapshot for the mixmind repo. AGENTS.md points here; this
file is rewritten wholesale when the state changes. Operating rules live in
AGENTS.md, not here.

## Verified at 72fa12b

Measured, not assumed — reproduce with `cmake --build build && ctest --test-dir build`.

- All 7 targets build clean: MixMind, Scope, Meter, EQT, Reflex, ThreeFX, Neat.
- Suite green: **39 cases / 11921 assertions**, ctest 100%.
- Formats: VST3 + AU + Standalone. AAX deferred to V2.
- JUCE 8.0.6 via FetchContent, C++17, Catch2 in `tests/` behind
  `MIXMIND_BUILD_TESTS`.
- CI (`.github/workflows/ci.yml`) is macOS-only on purpose: Ninja Release build
  of every target, the test suite, then `auval -v aufx <code> MXMN` on all seven
  Audio Units. auval is the only check that proves Logic can load the bundle.

## Code (post-cleanup, Sep 2026)

- **AI stack removed** — ApiClient, ChatComponent, ContextPanel, AIAnalysis
  are deleted from the MixMind target. No network code anywhere under
  `Source/`: the target builds with `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0`,
  `JUCE_REPORT_APP_USAGE=0`.
- **ShaperProcessor** — linear-phase FIR match-EQ (kTapCount=1024, kLatency=512,
  1/3-octave smoothing). Manual = traceTarget − live; Auto = ref − live. One
  engine serves both.
- **ChannelMode** — Stereo / Left / Right / Mid / Side pick which spectra feed
  the match, persisted as a non-automatable parameter. The dry path is delayed
  to match the FIR so non-stereo modes stay phase-coherent.
- **MatchState** — the match curve and the analysed reference serialise into the
  session, so the filter survives a save/load without the editor open.
- **LoudnessMeter** — BS.1770-4 (K-weighting, 400 ms gating, −70/−10 LU gates,
  4× true-peak). Shared with AudioAnalyzer (Scope/Meter/EQT).
- **AudioAnalyzer / ReferenceAnalyzer** — honest LUFS; both numBins == 1024.
- **TelemetryCanvas** — trace v-space is linear in dB (0..1 ↔ −100..0 dBFS);
  `buildTargetFromCurve` converts display → dB → linear magnitude, which
  `buildMatchFilter` then subtracts in dB against the live bins.
- **CMakeLists** — ShaperProcessor + LoudnessMeter in MixMind; LoudnessMeter in
  Scope/Meter/EQT.

## Known deviations (pre-existing, not yet fixed)

Recorded so they are not rediscovered as surprises. All four are also listed in
`pi-warden.md`, which will flag the file on the next edit — deliberately, so
touching them means fixing them.

1. `Source/LoudnessMeter.cpp:131-161` — `pushWindow`/`pushGatingSegment` run on
   the audio thread and use `std::deque::push_back`/`pop_front` with no reserve;
   `gateBlocks` grows for the length of the session (re-gating needs the whole
   history). Per-block cost is small and bounded, but it is allocator traffic on
   the audio thread.
2. `Source/ShaperProcessor.cpp:52-55` — every block takes a `SpinLock` and
   copies 4 KB of coefficients into `curTaps`. The writer side is message-thread
   only (`prepareToPlay`, the 250 ms timer, `setStateInformation`), so the lock
   is uncontended in practice; the copy is still avoidable. Related hazard at
   `:35`: a second design inside one block window computes `back = 1 − activeIdx`
   — the buffer the reader may hold. A writer-side handshake (three buffers, or
   an in-use flag) removes both.
3. `Source/PluginEditor.cpp:208` → `Source/TelemetryCanvas.cpp:62` — the 60 Hz
   editor timer reads `AudioAnalyzer::getFFTBins()` (`Source/AudioAnalyzer.h:21`,
   returns `fftAvg`, 1024 floats) while the audio thread writes it. Scalars
   (`getLufs`, `getStereoWidth`, `getPhaseCorr`, `getCrestFactor`) use
   `Atomic<float>`; the array read is the one that does not.
4. `Source/PluginEditor.cpp:239-254` — `ReferenceAnalyzer::loadFile()` runs in
   the file-dialog callback on the message thread: it decodes up to 10 minutes
   of audio and runs the whole BS.1770 + spectra pass inline, so the UI (and a
   plug-in host's window) freezes until it returns. Belongs on a background
   thread with progress.

## Rules and docs

Three files, three jobs — keep them in sync when a rule changes.

- `AGENTS.md` — operating doc: commands, boundaries, DSP/metering rules.
- `pi-warden.md` — the same rules in machine-checkable form (one rule per
  heading, 25 rules), plus the four deviations above.
- `.pi/skills/mixmind-dsp/` — the reasoning behind the DSP rules, with an audit
  checklist.

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

## Distribution gaps

Nothing here blocks a build; all of it blocks a public release.

- Binary is **arm64-only** (`lipo -info` reports one architecture), so Intel Macs
  cannot load it. One line: `set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")`.
- No Windows or Linux coverage; `build_pkg.sh` is macOS-only
  (codesign → notarytool → staple → pkgbuild).
- No `pluginval --strictness-level 10` pass.
- No sanitizer job. ThreadSanitizer would catch deviation 3 directly.
- No `LICENSE` file. `README.md` was added with this snapshot.

## Landing state

- `main` is `30e199c`, untouched. Everything described above lives on
  `consolidate/land-shaper-units` (28 commits ahead of main, pushed to origin).
- Three stranded branches were merged in rather than left islanded:
  `docs/honest-copy` (public-surface rewrite), `chore/untrack-logo`,
  `fix/proxy-remove-chat` (drop `/api/chat`, stop leaking status). The copy and
  the proxy fix now travel with the state doc that describes them.
- Three commits carry `[pi] ` messages (`2bc685c`, `4d3c097`, `62be11e`) — real
  content, non-Conventional messages. Squash-merge when landing if history
  legibility matters.

## Last milestone

`focus-telemetry-v3` (Aug 2026): rainbow master spectrum, AI demoted to an
optional collapsed panel. The Sep 2026 cleanup (above) removes the AI panel
entirely and lands the honest shaper/metering; tags `focus-telemetry-v1..v3`
and `honest-dsp-v1` exist.

## Open / next

- Fix the four known deviations before the first public release.
- Rebuild + repackage MixMind (settle the version story: CMake 1.0.0 vs site
  4.3.0) before any public download returns.
- Decide the license model before quoting $9/mo or $99:
  - signed offline keys (Ed25519) for perpetual licences — no server in the
    path; or
  - short-lived signed tokens + refresh if subscriptions need revocation.
  The `MM-` prefix check is not a gate and should not be sold as one.
  Constraint: JUCE 8 is dual-licensed AGPLv3 or commercial, so a closed-source
  paid build requires a JUCE commercial licence.
- Wire a real checkout (Stripe Payment Link or webhook → key email) and route
  `/purchase` at the edge.
- Universal macOS binary, a pluginval gate, and a sanitizer job.
- Logo + branding for the suite.
- Synth focus group still gated (5th group behind the default 4).
- Optional: iridescent band blending on the rainbow master.
