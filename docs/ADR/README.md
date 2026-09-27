Version: 1.1
Last Reviewed: 2026-09-27
Owner: Founder

# Architecture Decision Records

One decision per file, `ADR-NNN-short-title.md`, numbered in order.

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
