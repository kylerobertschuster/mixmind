# ADR-006: MixMind v1.0 Is Mix Doctor

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
The first v1.0 scope (in `docs/STATE.md` before this ADR) listed
infrastructure: the AI queue, the firewall, the OpenGL canvas, the SVF
filters. After VISION and ADR-001 that answered "what ships in v1.0?" with
plumbing instead of the product. It also conflicted with the roadmap:
AGENTS.md put AI in v1.0 while the priority list put AI coaching at
Priority 3.

## Decision
v1.0 ships **Mix Doctor**. Everything in it exists to support Mix Doctor.

**Required for v1.0**
- Mix Doctor report (#1)
- Severity scoring (#1)
- Analysis engine: measurement snapshots and the rolling window Mix Doctor
  observes (#2, #5)
- Frequency masking (#3)
- Reference track intelligence (#4)
- AI explanations of findings
- Approval gate (ADR-003)

**Nice-to-have (v1.x; never blocks v1.0)**
- Translation prediction (#6)
- Session history and trends: the longer 30–60 s windows (#2)
- Section detection (#7)
- AI coaching

**The v1.0 AI role**: Analysis → Diagnostics → Mix Doctor findings → AI
explanation. The AI translates observations into useful language.
- ✓ Explain findings in the producer's language
- ✓ Write the report's prose from the findings (the findings themselves come
  from the diagnostic engine — ADR-002)
- ✓ Recommend actions, applied only on the user's approval (ADR-003)
- ✗ Autonomous mixing
- ✗ Autonomous mastering
- ✗ Autonomous parameter application
- ✗ Coaching (later)

## Consequences
- The earlier v1.0 list in `docs/STATE.md` is replaced; those items are
  foundation work that is already done.
- The roadmap conflict is resolved: v1.0's AI is explanation; coaching is
  nice-to-have.
- AI explanations need their own payload. The firewall (ADR-004) accepts
  only parameter and trace suggestions today; an explanation schema (text
  keyed by finding ID, with length limits) has to be added, and it must not
  let the AI introduce facts the findings don't contain (ADR-002).
- ADR-005 still holds: v1.0 must be complete with no model and no network;
  explanations are the only part that goes quiet offline.
- Open, not decided by this ADR:
  - **Masking's v1.0 form.** Measuring which source masks which needs
    per-source signals (multi-instance, #3); the mix bus alone can only
    report symptoms. Decide before work on #3 starts.
  - **Offline file analysis (#8)** is in neither list. Offline *function*
    is required by ADR-005; analysing a bounced file is not yet placed.
