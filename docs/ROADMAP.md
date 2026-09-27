Version: 1.1
Last Reviewed: 2026-09-27
Owner: Founder

# MixMind Roadmap

Priorities serve Mix Doctor (ADR-001); the v1.0 cut is
[ADR-006](ADR/ADR-006-v1-is-mix-doctor.md). Progress lives in the issues and
`docs/STATE.md`, not here.

## v1.0 — Mix Doctor

### Required

| Item | Issue | Status |
|---|---|---|
| Mix Doctor report | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) | not started |
| Severity scoring | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) | not started |
| Analysis engine: measurement snapshots, the window Mix Doctor observes, audio-thread allocation guard | [#2](https://github.com/kylerobertschuster/mixmind/issues/2), [#5](https://github.com/kylerobertschuster/mixmind/issues/5) | partial — analyzers and BS.1770 meter exist; snapshots, window and guard don't (fixes AR-001) |
| Frequency masking | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) | not started — v1.0 form undecided (below) |
| Reference track intelligence | [#4](https://github.com/kylerobertschuster/mixmind/issues/4) | partial — analysis, caching and match EQ exist; comparison report doesn't |
| AI explanations of findings | — | partial — worker and firewall exist; no explanation payload, model backend or UI |
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

## v1.x — nice-to-have

| Item | Issue |
|---|---|
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

- **Masking's v1.0 form** (#3): multi-instance measurement of which source
  masks which, or mix-bus symptoms only?
- **Offline file analysis** (#8): not placed by ADR-006 — v1.0 or v1.x?
- **AI explanations**: model provider (local / cloud / `proxy/`), the
  explanation payload the firewall accepts, and an issue to track it.
