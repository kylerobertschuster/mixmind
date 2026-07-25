# JuicePipe Proxy

Node.js backend for MixMind by JuicePipe. Provides DeepSeek-powered AI
routing, license management, rate limiting, and health monitoring.

## Quick Start

```bash
cp env.example .env
# Edit .env with your DEEPSEEK_API_KEY
npm install
node server.js
```

## Endpoints

| Method | Path | Description |
|--------|------|-------------|
| GET | /health | Health check |
| GET | /healthz | Detailed health (API key status, license counts) |
| POST | /api/chat | Send messages to DeepSeek |
| POST | /validate | Validate a license key |
| POST | /admin/generate-license | Generate new license keys |
| POST | /admin/revoke-license | Revoke an existing license |

## Configuration

All configuration is done via environment variables in `.env`:

- `DEEPSEEK_API_KEY` — Required. Your DeepSeek API key.
- `ADMIN_SECRET` — Protects admin endpoints
- `PORT` — Server port (default: 3000)
- `LICENSES_FILE` — Path to license store JSON file

## Model

All chat requests route to DeepSeek (`deepseek-chat`) for fast, accurate
mixing advice driven by real-time audio analysis. The plugin sends its
spectral analysis as context with every request, so the model has awareness
of frequency balance, loudness, and dynamics.
