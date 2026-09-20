# JuicePipe Proxy

Node.js backend for JuicePipe licensing: license store, admin issuance,
Stripe-gated purchase, rate limiting, and health monitoring.

The DeepSeek chat proxy (`POST /api/chat`) was removed on 2026-09-20. MixMind's
AI chat was deleted from the plugin in September 2026 and nothing shipped calls
that route any more. Keeping it live was paid API surface and attack surface
for a feature that no longer exists. If the chat ever comes back, it comes back
with its own service, its own quota, and per-user accounting — not a route
bolted onto the license server.

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | /health | Liveness check |
| GET | /healthz | Liveness + version (public) |
| POST | /validate | Validate a license key |
| POST | /admin/generate-license | Generate new license keys (admin secret) |
| POST | /admin/revoke-license | Revoke an existing license (admin secret) |
| POST | /purchase | Mint a license for a paying customer (Stripe signature or admin secret) |

## Quick Start

```bash
cp env.example .env
# Edit .env with ADMIN_SECRET (and STRIPE_WEBHOOK_SECRET once Stripe is wired)
npm install
node server.js
```

## Configuration

All configuration is done via environment variables in `.env`:

- `ADMIN_SECRET` — Protects admin endpoints. If unset, they are disabled.
- `STRIPE_WEBHOOK_SECRET` — When set, `/purchase` accepts only requests with a
  valid Stripe signature. When unset, `/purchase` requires `ADMIN_SECRET`.
- `PORT` — Server port (default: 3000)
- `LICENSES_FILE` — Path to license store JSON file

## License model

Keys are `MM-` + 16 hex characters and stored in `LICENSES_FILE`. Today the
plugin checks the `MM-` prefix locally and never contacts this server; the
`/validate` endpoint exists but is unused by any shipped client. Whether the
plugin gets real server or signature-based validation is an open decision —
see `docs/STATE.md`.
