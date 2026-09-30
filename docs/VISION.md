Version: 1.2
Last Reviewed: 2026-09-30
Owner: Founder

# MixMind Vision

Highest authority of the project documents (see AGENTS.md, "Documents and
authority"). Clarifications and wording may change freely; changes to the
mission, the audience or the product category need founder review.

## Mission

MixMind is an understanding-first audio platform that helps producers
understand why a mix is not working and what to try next.

It exists to reduce the distance between

> "I know something sounds wrong."

and

> "I know exactly what to fix."

MixMind is an intelligent second pair of ears inside the DAW. It is not an
auto-mixing or auto-mastering system, and it is not an AI chatbot with audio
attached: nothing changes the audio unless the producer turns it on or
approves it.

## Principles

- MixMind is an understanding-first audio platform.
- Its primary value is diagnosis.
- Its processing tools exist to help users act on those diagnoses.
- AI may explain findings and prepare actions.
- Users remain responsible for applying changes.
- All automated actions must pass through explicit user approval.
- Offline diagnostics remain functional.
- DSP remains the source of truth.

## Philosophy

Enhance producers; do not replace them.
Teach; do not obscure.
Explain; do not automate creativity.
Build confidence; do not create dependency.

A producer should leave MixMind feeling smarter than before. The best outcome
is a user who eventually needs MixMind less because they learned from it.

## Positioning

| | Leads with | Says |
|---|---|---|
| FabFilter | processing | "Here's an EQ." |
| Ozone | processing (mastering assistance) | "Here's a master." |
| **MixMind** | **understanding** | **"Here's why this EQ move helps."** |

MixMind competes primarily through diagnosis and understanding, while
providing processing tools — the reference match EQ and the parametric EQ —
that are guided by those findings.

## Product identity

MixMind is diagnostic, educational, transparent, actionable and
evidence-based. It is not a magic button, a mastering robot, or a
replacement for skill and experience.

## Source of truth

DSP discovers facts. Diagnostics interpret facts. AI explains facts.

Measurements are the source of truth; the AI layer never is. Every
recommendation must be directly supported by measurable observations, and no
comparison is made against a norm that has not been measured.

## How MixMind decides

Every capability is a bounded decision ("is the low end louder than the
reference's?", not "analyse the low end"), made in one order:

```
Audio → Measurements → Diagnostic decision (with its evidence) → Confidence
      → Recommended action → Producer's approval → Optional processing
```

Never reverse it: no finding without a measurement behind it, no
explanation without a finding, no change to the audio without the producer.

## Flagship: Mix Doctor

Mix Doctor is the center of the product; everything else should strengthen
it. It detects, ranks and explains issues and teaches solutions. Every
finding carries:

1. **Observation** — what was measured, where (frequency, time, section).
2. **Impact** — why it matters to the listener.
3. **Severity** — how much it matters, relative to the reference or a stated standard.
4. **Confidence** — how sure MixMind is, from the evidence (ADR-007). When it
   knows, it says it knows; when it suspects, it says it suspects.
5. **Recommended actions** — what to try first, in the producer's hands.

Example:

> **Low end 4 dB above the reference** · Severity: High · Confidence: High
> Observation: 40–120 Hz is 4.1 dB louder than the reference, loudness-matched.
> Impact: masks the kick's attack and costs headroom.
> Try: a low shelf of −3 dB at 100 Hz, then re-run Mix Doctor.

## Target user

**Primary:** the capable but frustrated producer — can hear problems, cannot
always identify causes, wants to improve, wants guidance rather than
automation.

**Secondary:** mix engineers, students, educators, sound designers, audio
enthusiasts.

## Core product questions

Before building a feature, ask:

1. Does this help identify a problem?
2. Does this help explain a problem?
3. Does this help solve a problem?
4. Does this help users learn?
5. Does this make a finding more accurate, or its confidence better founded?

If not, challenge whether it should exist.

## Success

Not AI usage, chat messages, the number of insights or visualizations,
recommendations made or EQ moves applied. Success is:

> Did MixMind correctly identify the problem the producer was struggling to describe?

The product wins when a producer says "it found exactly what was wrong",
not "it gave me an AI response". Understanding is the moat; diagnosis is the
product; AI is the interface; the producer stays in control.

## Long-term goal

Build the most trusted audio diagnostic engine in music production. When
producers get stuck, MixMind should be the first tool they open — not
because it makes decisions for them, but because it helps them understand
the decisions they need to make.
