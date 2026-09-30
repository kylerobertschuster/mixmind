Version: 1.3
Last Reviewed: 2026-09-30
Owner: Founder

# Architecture Decision Records

One decision per file, `ADR-NNN-short-title.md`, numbered in order.
Accepted ADRs rank below `ARCHITECTURE.md` and above `AGENTS.md` (AGENTS.md,
"Documents and authority").

- **When.** A principle, a threading or real-time contract, the AI's powers
  or what users are promised changes. Not for rule thresholds: those live in
  the code, its tests and `ARCHITECTURE.md`.
- **Append-only.** Never edit an accepted decision. To change it, write a new
  ADR that supersedes it and set the old one's status to
  `Superseded by ADR-NNN` (the only edit an old ADR ever gets).
- A change that contradicts an Accepted ADR needs a new ADR first.
- Sections: Status, Date, Context, Decision, Consequences.

| ADR | Decision | Status |
|---|---|---|
| [001](ADR-001-mix-doctor-is-primary-product-surface.md) | Mix Doctor is the primary product surface | Accepted |
| [002](ADR-002-dsp-is-source-of-truth.md) | DSP is the source of truth | Accepted |
| [003](ADR-003-ai-recommendations-require-user-approval.md) | AI recommendations require user approval | Accepted |
| [004](ADR-004-strict-json-firewall.md) | Strict JSON firewall | Accepted |
| [005](ADR-005-offline-diagnostics-must-function.md) | Offline diagnostics must function | Accepted |
| [006](ADR-006-v1-is-mix-doctor.md) | MixMind v1.0 is Mix Doctor | Accepted |
| [007](ADR-007-diagnostic-confidence.md) | Every finding carries severity and confidence | Accepted |
| [008](ADR-008-v1-masking-is-mix-bus.md) | v1.0 masking is mix-bus; track-aware in v1.1; offline file analysis first v1.x | Accepted |
