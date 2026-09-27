# ADR-005: Offline Diagnostics Must Function

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
A plugin that needs the internet fails in the studio: sessions open offline,
on flights, behind firewalls, when a service is down. "Internet down =
plugin useless" is unacceptable for a tool producers rely on.

## Decision
Measurement (Layer 1) and diagnostics (Layer 2) always run locally and never
depend on the AI layer or the network. The AI (Layer 3) is optional at
runtime: without a model or a network, MixMind still produces every
measurement and every Mix Doctor finding, and the AI panel says explanation
is unavailable.

## Consequences
- No feature may require a network round-trip to produce a finding.
- AI failures (no backend, timeout, error, rejected output) come back as
  results and never block or freeze the plugin (tested in the `AI` category).
- Offline file analysis (#8) uses the same engines as real time.
- Licensing (#9) must work offline too.
