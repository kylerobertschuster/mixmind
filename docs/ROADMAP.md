Version: 1.4
Last Reviewed: 2026-09-28
Owner: Founder

# MixMind Roadmap

Priorities serve Mix Doctor (ADR-001); the v1.0 cut is
[ADR-006](ADR/ADR-006-v1-is-mix-doctor.md), refined by
[ADR-007](ADR/ADR-007-diagnostic-confidence.md) (confidence) and
[ADR-008](ADR/ADR-008-v1-masking-is-mix-bus.md) (masking, offline analysis).
Progress lives in the issues and `docs/STATE.md`, not here.

## v1.0 — Mix Doctor

### Required

| Item | Issue | Status |
|---|---|---|
| Mix Doctor report | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) | partial — the report is built (findings ranked, Markdown); no panel in the editor yet |
| Severity and confidence scoring (ADR-007) | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) | partial — six rules with explicit, tested thresholds: true peak, tone against the reference (level-matched), density, low end in mono, phase, loudness; congestion rules come with #3 |
| Analysis engine: measurement snapshots, the window Mix Doctor observes, audio-thread allocation guard | [#2](https://github.com/kylerobertschuster/mixmind/issues/2), [#5](https://github.com/kylerobertschuster/mixmind/issues/5) | done except the allocation guard (#5) — snapshots (AR-001 resolved), 100 ms frames, 60 s history with BS.1770 / EBU 3342 statistics |
| Frequency masking — mix-bus congestion diagnostics with confidence (ADR-008) | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) | not started |
| Reference track intelligence | [#4](https://github.com/kylerobertschuster/mixmind/issues/4) | partial — analysis, caching and match EQ exist; comparison report doesn't |
| AI explanations of findings | [#11](https://github.com/kylerobertschuster/mixmind/issues/11) | partial — worker and firewall exist; no explanation payload, model backend or UI |
| Approval gate | — | done (ADR-003) |

Offline function is required too (ADR-005): v1.0 must be complete with no
model and no network.

### The v1.0 AI

Analysis → Diagnostics → Mix Doctor findings → AI explanation.

| In v1.0 | Not in v1.0 |
|---|---|
| ✓ Explain findings | ✗ Autonomous mixing |
| ✓ Write the report's prose from findings | ✗ Autonomous mastering |
| ✓ Recommend actions (applied on approval) | ✗ Autonomous parameter application |
| | ✗ Coaching (nice-to-have) |

## v1.1 — track-aware diagnostics (ADR-008)

Instance discovery, cross-instance spectral exchange, actual kick-vs-bass
identification — [#3](https://github.com/kylerobertschuster/mixmind/issues/3). v1.0 finds congestion; v1.1 names the tracks
causing it.

## v1.x — nice-to-have

| Item | Issue |
|---|---|
| Offline file analysis — first v1.x feature after launch (ADR-008) | [#8](https://github.com/kylerobertschuster/mixmind/issues/8) |
| Translation prediction | [#6](https://github.com/kylerobertschuster/mixmind/issues/6) |
| Session history and trends (30–60 s windows) | [#2](https://github.com/kylerobertschuster/mixmind/issues/2) |
| Section detection | [#7](https://github.com/kylerobertschuster/mixmind/issues/7) |
| AI coaching | — |

## Later

Action plans, local AI inference, adaptive learning.

## Release work (needed to sell v1.0; not product features)

| Item | Issue |
|---|---|
| Plugin settings and licensing UX | [#9](https://github.com/kylerobertschuster/mixmind/issues/9) |
| Commercial beta program | [#10](https://github.com/kylerobertschuster/mixmind/issues/10) |

## Open questions (raised, not resolved)

- **The v1.0 model provider** for AI explanations (local / cloud / `proxy/`)
  — tracked in [#11](https://github.com/kylerobertschuster/mixmind/issues/11).
- **Confidence calibration** (ADR-007): the labelled set that earns a rule
  its numeric percentage — the beta's "this finding was wrong" feedback
  ([#10](https://github.com/kylerobertschuster/mixmind/issues/10)) is the first source.
- **Launch page** (`site/mixmind.html`) describes v1.0 and must not go live
  before v1.0 ships; it leaves translation out until [#6](https://github.com/kylerobertschuster/mixmind/issues/6) ships.
