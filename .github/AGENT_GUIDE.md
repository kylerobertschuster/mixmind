# AGENT_GUIDE.md

This file explains how to work with Pi (AI coding agent) on MixMind.

Essentials
- Keep changes small (<300 LOC) per PR.
- Every change must include: a test (unit or smoke), lint pass, and a short PR description.
- Never commit secrets. If a secret is needed for CI, set it in repository secrets.

Prompt templates (use these when asking Pi)
- Feature implementation:
  "Implement <feature>. Change only files in <paths>. Add unit tests that assert <conditions>. Provide a single commit with message 'feat: <short description>'. Open PR titled '<short description>' with a 2-line summary."
- Fix/bug:
  "Fix <bug>. Add test reproducing the bug and the fix. Keep changes minimal."
- Tests:
  "Add unit tests for <module> covering <cases>. Use existing test harness and assert <conditions>."

PR checklist (required before merge)
- [ ] Tests present and passing
- [ ] Linting OK
- [ ] CI green
- [ ] No large binaries added without LFS
- [ ] No secrets in diffs
- [ ] One reviewer approved and sanity-checked locally

