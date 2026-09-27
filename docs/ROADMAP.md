Version: 1.0
Last Reviewed: 2026-09-27
Owner: Founder

# MixMind Roadmap

Priorities serve Mix Doctor (ADR-001). Each item is a GitHub issue; progress
lives there and in `docs/STATE.md`, not here.

## Foundation — done

Honest BS.1770 metering, reference analysis and caching, 2048-tap
linear-phase match EQ (linked and mid/side), 8-band analog-matched EQ,
non-finite guard, GPU-rendered editor, AI worker with strict firewall and
user approval.

## Priority 1 — become indispensable

| Item | Issue |
|---|---|
| Mix Doctor MVP (incl. severity scoring) | [#1](https://github.com/kylerobertschuster/mixmind/issues/1) |
| Rolling analysis engine | [#2](https://github.com/kylerobertschuster/mixmind/issues/2) |
| Frequency masking detection | [#3](https://github.com/kylerobertschuster/mixmind/issues/3) |
| Reference track intelligence | [#4](https://github.com/kylerobertschuster/mixmind/issues/4) |

## Priority 2 — serious audio intelligence

| Item | Issue |
|---|---|
| Translation prediction | [#6](https://github.com/kylerobertschuster/mixmind/issues/6) |
| Section detection | [#7](https://github.com/kylerobertschuster/mixmind/issues/7) |
| Session trend analysis | extends [#2](https://github.com/kylerobertschuster/mixmind/issues/2) |
| Offline diagnostic mode | [#8](https://github.com/kylerobertschuster/mixmind/issues/8) |

## Priority 3 — coaching

AI coaching, action plans, local AI inference, adaptive learning. No issues
yet.

## Cross-cutting

| Item | Issue |
|---|---|
| Audio-thread allocation guard and snapshots | [#5](https://github.com/kylerobertschuster/mixmind/issues/5) |
| Plugin settings and licensing UX | [#9](https://github.com/kylerobertschuster/mixmind/issues/9) |
| Commercial beta program | [#10](https://github.com/kylerobertschuster/mixmind/issues/10) |

## Open conflicts (raised, not resolved)

- **Where AI sits.** AGENTS.md and `docs/STATE.md` put AI integration in the
  v1.0 scope; this list puts AI coaching at Priority 3. Needs a founder
  decision: is v1.0's AI the explanation of Mix Doctor findings (and so
  needs #1 first), or does v1.0 ship before Mix Doctor?
- **The v1.0 cut.** `docs/STATE.md` still lists the earlier v1.0 items;
  whether v1.0 is re-scoped around Priority 1 is undecided.
