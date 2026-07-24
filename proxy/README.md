# JuicePipe Proxy

Node.js backend for MixMind by JuicePipe. Provides AI model routing,
license management, rate limiting, and health monitoring.

## Quick Start

```bash
cp env.example .env
# Edit .env with your API keys
npm install
node server.js
```

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | /health | Health check |
| POST | /api/chat | Send messages to AI model |
| POST | /validate | Validate a license key |
| POST | /admin/generate-license | Generate new license keys |
| POST | /admin/revoke-license | Revoke an existing license |

## Configuration

All configuration is done via environment variables in `.env`:

- `ANTHROPIC_API_KEY` — Required for Premium tier (Claude)
- `GROQ_API_KEY` — Required for Standard tier (Llama)
- `ADMIN_SECRET` — Protects admin endpoints
- `PORT` — Server port (default: 3000)
- `LICENSES_FILE` — Path to license store JSON file

## Model Routing

- **Standard tier**: Routes to Groq (llama-3.3-70b-versatile) — fast and cost-effective
- **Premium tier**: Routes to Anthropic (claude-sonnet-4-20250514) — deeper analysis

The plugin sends its real-time audio analysis as context with every request,
so the model has awareness of frequency balance, loudness, and dynamics.
