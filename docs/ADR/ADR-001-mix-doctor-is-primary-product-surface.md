# ADR-001: Mix Doctor Is the Primary Product Surface

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
MixMind grew as an analyzer plus a reference match EQ and a parametric EQ.
Measurements and processing alone are commodity features; FabFilter and Ozone
own processing. Producers get stuck because they cannot name what is wrong,
not because they lack an EQ.

## Decision
Mix Doctor — a report of ranked findings, each with observation, impact,
severity and recommended actions — is the center of the product. Other
features are prioritised by how much they strengthen it. The match EQ and
parametric EQ stay as the tools a producer uses to act on findings.

## Consequences
- Roadmap priorities follow Mix Doctor (docs/ROADMAP.md); a feature that does
  not help identify, explain or solve a problem, or help the user learn, is
  challenged.
- Mix Doctor does not exist yet; it is issue #1 and needs the analysis
  history (#2) and reference intelligence (#4).
