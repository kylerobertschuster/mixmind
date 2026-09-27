# ADR-002: DSP Is the Source of Truth

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
An AI-first design ("AI + FFT + chat") lets a language model state things
about a mix that nothing measured. Wrong advice presented confidently
destroys the trust a diagnostic tool depends on.

## Decision
DSP discovers facts, diagnostics interpret facts, AI explains facts — never
the reverse.
- Only the analysis engine measures; only the diagnostic engine creates
  findings. The AI explains findings and may prepare actions from them.
- Every recommendation cites the measurements behind it.
- No comparison against a norm that has not been measured: no "industry
  average" until a measured reference corpus exists; until then, compare
  with the user's own reference track or a published standard (BS.1770,
  true-peak ceilings).

## Consequences
- AI output that introduces a new claim is a bug, not a feature.
- Metering stays under the AGENTS.md metering rules (no proxies).
- The diagnostic engine has to carry evidence with every finding so the AI
  layer has something to explain.
