Version: 1.1
Last Reviewed: 2026-09-30
Owner: Founder

# Founder's Notes

These notes are intentionally informal. This is not product documentation;
it is the reasoning behind the product.

**Append-only.** Never remove founder observations or rewrite historical
reasoning. Add new dated entries at the end.

---

## 2026-09-27 — The original notes

### Original realization

At first the project looked like:

AI + FFT + Chat

The deeper realization was:

MixMind is not an analyzer.
MixMind is not a chatbot.
MixMind is a diagnostic engine.

### Important insight

Ozone helps users master.
FabFilter helps users see.
MixMind helps users understand.

This distinction should remain protected.

### Producer first

MixMind exists to enhance producers. Not replace them. Not automate them.
Not out-create them.

If a feature reduces learning or creative ownership, challenge it.

### Against black magic

Many production tools feel magical. The user receives a result. The process
is hidden.

MixMind should move in the opposite direction. Show observations. Show
reasoning. Show evidence. Teach.

### Telemetry matters

ChatGPT does not hear the mix. MixMind does. This is one of the strongest
competitive advantages.

MixMind has access to:

- Frequency content
- Dynamics
- Loudness
- Stereo image
- Transport information
- Reference tracks
- Session telemetry

The product should lean into this.

### The real competitor

The real competitor is not FabFilter. The real competitor is not Ozone.

The real competitor is experience. Experienced engineers can hear a problem
and identify it rapidly. MixMind attempts to make that expertise more
accessible.

### Mix Doctor is the product

Whenever roadmap discussions become noisy: return to Mix Doctor.

The question becomes: does this improve Mix Doctor? If yes, prioritize it.
If no, challenge it.

### The lightning factory

Mixing is often described as capturing lightning in a bottle. The reality is
that great producers consistently create conditions that lead to great
results.

MixMind helps producers build their own lightning factory. The goal is
intentional creation rather than accidental success.

### Founder reminder

Do not become distracted by more graphs, more AI, more automation.

Focus on better diagnosis, better explanations, better education, better
understanding.

Understanding is the moat. Not AI. Not DSP. Not automation. Understanding.

### Question to revisit often

If MixMind disappeared tomorrow, what would producers miss?

The answer should never be: "The chatbot."

The answer should be: "It found problems I couldn't describe."

That is the standard.

---

## 2026-09-27 — Diagnosis first, not diagnosis only

I was too aggressive saying "MixMind competes through diagnosis, not
processing." Technically false for this repo.

Better: MixMind competes primarily through diagnosis and understanding, while
providing processing tools that are guided by those findings.

FabFilter → processing-first. Ozone → processing-first. MixMind →
understanding-first.

The EQ stays. The match engine stays. The difference is:

FabFilter: "Here's an EQ."
MixMind: "Here's why this EQ move helps."

That's a position worth keeping.

---

## 2026-09-30 — MixMind is a decision engine

Reading about Jev (an API for typed decisions: state → decision → confidence
→ action) made the shape clear. MixMind works the same way:

Audio state → diagnostic decision → confidence → suggested action.

Every feature is a bounded decision with measurable evidence, a confidence
and a recommended action. "Does low-end masking exist?" "Will this mix
translate poorly to small speakers?" "How far is this mix from the
reference?"

Copilot drafted this as a separate Build Doctrine. Most of it was already
decided (VISION, ADR-002 to ADR-005, AGENTS.md), so the new parts went into
the documents we have instead of a second rulebook:

- VISION: the decision pipeline, the sharper success line.
- AGENTS: define the decision and its evidence first; build measurement →
  finding → confidence → explanation; work must improve Mix Doctor.
- Accepted ADRs now rank between ARCHITECTURE and AGENTS.
- ADRs are for contracts (principles, threading, the AI's powers, what users
  are promised), not for every threshold.

Kept ADR-007 over the draft's "8/10, 91 %" examples: until a rule is checked
against labelled mixes, confidence is High / Medium / Low. A number nobody
has checked is invented.

Jev itself is a framing, not a component: a cloud service that reads text
can't sit in a diagnostic path that has to work offline from measurements
(ADR-002, ADR-005).
