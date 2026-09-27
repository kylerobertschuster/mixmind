# ADR-003: AI Recommendations Require User Approval

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
The AI worker can propose parameter changes (EQ bands, match amount, the
trace). Applying them automatically would make MixMind an auto-mixer, take
creative ownership away from the producer, and let a model's mistake reach
the audio unseen.

## Decision
AI may explain findings and prepare actions. Users remain responsible for
applying changes. Every AI-prepared action is a **suggestion** until the user
explicitly approves it.

Implementation (`MixMindProcessor`):
- An accepted result from `AiWorker` becomes the pending suggestion
  (`getAiSuggestion`); only the latest result's suggestion is kept, and a newer
  result — accepted or not — replaces it.
- `approveAiSuggestion()` (the user's click) applies it once, as host-visible
  parameter gestures and a trace edit, so it is undoable and automatable like
  any user edit. `dismissAiSuggestion()` discards it.
- Pending suggestions are not saved with the session.

## Consequences
- Nothing the AI produces changes the audio without a user action (tested:
  pending, dismissed and superseded suggestions change nothing).
- The UI must show what a suggestion would change before the click.
- Any future "auto" behaviour needs a new ADR.
