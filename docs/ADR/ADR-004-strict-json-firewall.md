# ADR-004: Strict JSON Firewall

Status: Accepted · Date: 2026-09-27 · Owner: Founder

## Context
Model output is untrusted text. A NaN, an out-of-range value or a
misread number that reached the DSP could produce silence, full-scale
noise or a 24 dB cut. Fuzzing showed that `juce::JSON::parse` alone is not
safe: it reads `-x` as −72 and `-.5` as 15, wraps integers past int64 to the
wrong sign, accepts `- 5` and single-quoted strings, ignores text after the
object, and recurses without a depth limit (a stack overflow in the host).

## Decision
Every AI payload passes `AiFirewall` on the worker thread before it enters
the lock-free queue:
1. Size limit (64 KB), then a strict RFC 8259 check (numbers, literals,
   strings, nesting depth ≤ 8, nothing after the object) before JUCE parses it.
2. Schema: only `parameters` (by parameter ID) and `trace` (Hz, dB points).
   Anything else — unknown fields or IDs, wrong types, filter coefficients,
   prose, code fences — rejects the payload.
3. All-or-nothing: one bad field rejects the whole payload.
4. NaN / inf reject. Continuous values clamp to the parameter's own range;
   choices and switches must be exact (clamping "24" meaning 24 dB/oct to the
   last slope would pick the wrong setting). The trace clamps to
   20 Hz–20 kHz and −100..0 dB, sorted, points ≥ 1 % apart.
5. The AI never supplies filter coefficients; it proposes band settings and
   the analog-matched designer computes them.

## Consequences
- Firewall behaviour is covered by the `AI` test category, including a
  9000-case fuzz: nothing non-finite, unknown or out of range is ever
  accepted.
- A backend must ask its model for JSON only; replies wrapped in prose fail.
