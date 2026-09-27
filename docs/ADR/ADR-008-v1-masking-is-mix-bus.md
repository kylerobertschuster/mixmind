# ADR-008: v1.0 Masking Is Mix-Bus Diagnostics; Track-Aware Masking Is v1.1

Status: Accepted · Date: 2026-09-27 · Owner: Founder

Resolves the two questions ADR-006 left open.

## Context
Measuring which source masks which needs each source's own signal: MixMind
instances on individual tracks that find each other and exchange spectra.
Hosts differ in how they run plugins (shared process, sandboxed, out of
process), so instance discovery is a real compatibility risk for a first
release. Building the technically complete version first would delay the
useful version.

Offline file analysis (#8) was in neither of ADR-006's lists.

## Decision
**v1.0 — mix-bus diagnostics.** MixMind observes, from the mix bus:
low-end congestion, midrange congestion, transient suppression, spectral
crowding and reference deviation. Each finding carries confidence (ADR-007),
lists *potential* sources and recommends a next step to check. It never
claims to know which tracks are involved.

> Low-end congestion · Confidence: Medium
> Observation: low-frequency content is suppressing punch.
> Potential sources: kick/bass interaction, over-compressed low-end bus,
> excessive sub energy.
> Next step: inspect the kick and bass relationship.

**v1.1 — track-aware diagnostics.** Instance discovery, cross-instance
spectral exchange, and actual kick-vs-bass identification.

**Offline file analysis (#8) is not in v1.0.** It is the first v1.x feature
after launch: it serves "help me evaluate", while v1.0 serves "help me mix".

## Consequences
- v1.0 copy may say MixMind finds congestion and likely masking problems; it
  must not claim to identify the tracks causing them. v1.1 copy can.
- Issue #3 splits into the v1.0 mix-bus part and the v1.1 track-aware part.
- Congestion diagnostics below ~120 Hz probably need better low-end
  resolution (a 4096-point or multi-resolution analyzer). Changing the live
  FFT also changes `mapToGrid` and the match-EQ grid, so that needs a design
  note first (AGENTS.md DSP rules).
