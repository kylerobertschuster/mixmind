# ADR-007: Diagnostic Confidence

Status: Accepted · Date: 2026-09-27 · Owner: Founder

Related: ADR-001 (Mix Doctor is the primary product surface), ADR-002 (DSP is
the source of truth), ADR-003 (AI recommendations require user approval),
ADR-005 (offline diagnostics must function), ADR-006 (v1.0 is Mix Doctor),
ADR-008 (v1.0 masking is mix-bus diagnostics).

## Context
MixMind diagnoses conditions that often cannot be known with certainty:
low-end congestion, masking, midrange density, reference deviations,
translation risks. In v1.0 most findings are inferred from the mix bus rather
than from track-level signals (ADR-008).

Without a confidence model, users assume every finding is equally certain.
A confident wrong answer costs more trust than an honest "probably".

## Decision
Every Mix Doctor finding carries **severity** and **confidence**, and they are
independent:

- Severity answers "how much does this matter?"
- Confidence answers "how sure is MixMind?"

### Measured vs inferred
- A finding that restates a **measurement** (true peak, loudness or band
  level against the reference) is limited only by the measurement conditions:
  enough signal, long enough.
- A finding that **infers a cause** (low-end congestion → kick/bass
  interaction) is less certain. It lists *potential* causes and a next step
  to check, rather than naming one cause.

### Where confidence comes from
Signal presence and quality, observation duration, agreement between
independent metrics, the strength of the reference comparison, and (from
v1.1) track-level evidence. Every finding lists the measurements behind it.
Confidence is never fabricated: a rule that cannot say why it is confident is
not confident.

### Presentation
- Always shown as **High / Medium / Low**, next to severity.
- A **numeric percentage** is shown only for rules calibrated against a
  labelled set: findings shown at 80 % must be right about 80 % of the time.
  Until a rule is calibrated, levels only. A percentage nobody has checked is
  a fabricated one.

### AI
The AI may explain confidence. It may not set or change it, and its output
does not carry severity or confidence at all — those are rendered from the
diagnostics (#11).

## Examples
| Finding | Severity | Confidence | Why |
|---|---|---|---|
| True peak above −1 dBTP (v1.0) | Medium | High | directly measured |
| Low-end congestion (v1.0). Potential causes: kick/bass interaction, over-compressed low-end bus, excess sub energy. Next step: inspect the kick and bass relationship. | High | Medium | inferred from the mix bus; several causes fit |
| Kick masked by bass (v1.1, track-aware) | 9/10 | High (96 % once calibrated) | measured on the kick and bass tracks themselves |

## Consequences
- Diagnostic rules carry a confidence rule next to their severity rule, both
  tested with synthetic signals (#1).
- Calibration needs labelled data: the beta's "this finding was wrong"
  feedback (#10) is the first source.
- Mix-bus diagnostics can ship in v1.0 without overclaiming (ADR-008), and
  track-aware diagnostics later raise confidence rather than change the
  model.
- Trade-offs: more diagnostic work, calibration work, UI space.

When MixMind knows, it says it knows. When it suspects, it says it suspects.
