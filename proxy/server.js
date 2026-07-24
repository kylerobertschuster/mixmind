import express from "express";
import Anthropic from "@anthropic-ai/sdk";
import Groq from "groq-sdk";
import { randomUUID } from "crypto";
import fs from "fs";
import path from "path";

// ─────────────────────────────────────────────────────────────────────────────
//  Config
// ─────────────────────────────────────────────────────────────────────────────
const PORT = process.env.PORT || 3000;
const ANTHROPIC_KEY = process.env.ANTHROPIC_API_KEY || "";
const GROQ_KEY = process.env.GROQ_API_KEY || "";
const LICENSES_FILE = process.env.LICENSES_FILE || "./licenses.json";
const ADMIN_SECRET = process.env.ADMIN_SECRET || "";

// Rate limiting: max requests per IP per window
const RATE_LIMIT_WINDOW_MS = 60_000;   // 1 minute
const RATE_LIMIT_MAX_REQ  = 60;         // 60 requests/min per IP

// ─────────────────────────────────────────────────────────────────────────────
//  Validate API keys at startup
// ─────────────────────────────────────────────────────────────────────────────
if (!ANTHROPIC_KEY) {
    console.warn("⚠️  ANTHROPIC_API_KEY is not set. Premium tier (Claude) will fail.");
}
if (!GROQ_KEY) {
    console.warn("⚠️  GROQ_API_KEY is not set. Standard tier (Llama) will fail.");
}
if (!ADMIN_SECRET) {
    console.warn("⚠️  ADMIN_SECRET is not set. License admin endpoints are disabled.");
}

const anthropic = ANTHROPIC_KEY ? new Anthropic({ apiKey: ANTHROPIC_KEY }) : null;
const groq = GROQ_KEY ? new Groq({ apiKey: GROQ_KEY }) : null;

// ─────────────────────────────────────────────────────────────────────────────
//  License store
// ─────────────────────────────────────────────────────────────────────────────
function loadLicenses() {
    if (!fs.existsSync(LICENSES_FILE)) fs.writeFileSync(LICENSES_FILE, "{}");
    return JSON.parse(fs.readFileSync(LICENSES_FILE, "utf8"));
}

function saveLicenses(data) {
    fs.writeFileSync(LICENSES_FILE, JSON.stringify(data, null, 2));
}

function validateLicense(key) {
    if (!key) return null;
    const licenses = loadLicenses();
    const license = licenses[key];
    if (!license || !license.active) return null;
    return license;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rate limiter (per-IP, in-memory)
// ─────────────────────────────────────────────────────────────────────────────
const requestCounts = new Map();

function rateLimit(req, res, next) {
    const ip = req.ip || req.connection?.remoteAddress || "unknown";
    const now = Date.now();

    if (!requestCounts.has(ip)) {
        requestCounts.set(ip, { count: 1, resetAt: now + RATE_LIMIT_WINDOW_MS });
        return next();
    }

    const entry = requestCounts.get(ip);
    if (now > entry.resetAt) {
        entry.count = 1;
        entry.resetAt = now + RATE_LIMIT_WINDOW_MS;
        return next();
    }

    entry.count++;
    if (entry.count > RATE_LIMIT_MAX_REQ) {
        return res.status(429).json({
            error: "Too many requests. Try again shortly.",
            retry_after_ms: entry.resetAt - now,
        });
    }

    next();
}

// Clean up stale entries every 5 minutes
setInterval(() => {
    const now = Date.now();
    for (const [ip, entry] of requestCounts.entries()) {
        if (now > entry.resetAt) requestCounts.delete(ip);
    }
}, 300_000);

// ─────────────────────────────────────────────────────────────────────────────
//  App
// ─────────────────────────────────────────────────────────────────────────────
const app = express();
app.use(express.json({ limit: "100kb" }));
app.use(rateLimit);

// ── Health check (lightweight) ───────────────────────────────────────────────
app.get("/health", (req, res) => {
    res.json({
        status: "ok",
        uptime: process.uptime(),
        timestamp: new Date().toISOString(),
    });
});

// ── Health check (detailed — includes API key status) ────────────────────────
app.get("/healthz", (req, res) => {
    const licenses = loadLicenses();
    const activeCount = Object.values(licenses).filter((l) => l.active).length;

    res.json({
        status: "ok",
        uptime: process.uptime(),
        timestamp: new Date().toISOString(),
        version: "1.0.0",
        api_keys: {
            anthropic: ANTHROPIC_KEY ? "configured" : "missing",
            groq: GROQ_KEY ? "configured" : "missing",
            admin: ADMIN_SECRET ? "configured" : "missing",
        },
        licenses: {
            total: Object.keys(licenses).length,
            active: activeCount,
        },
        rate_limiting: {
            window_ms: RATE_LIMIT_WINDOW_MS,
            max_per_window: RATE_LIMIT_MAX_REQ,
        },
    });
});

// ── Admin: Generate license keys ─────────────────────────────────────────────
app.post("/admin/generate-license", (req, res) => {
    const secret = req.headers["x-admin-secret"];
    if (!ADMIN_SECRET || secret !== ADMIN_SECRET) {
        return res.status(401).json({ error: "Unauthorized" });
    }

    const { tier = "standard", count = 1 } = req.body;
    const licenses = loadLicenses();
    const newKeys = [];

    for (let i = 0; i < count; i++) {
        const key = "MM-" + randomUUID().toUpperCase().replace(/-/g, "").substring(0, 16);
        licenses[key] = { tier, active: true, createdAt: new Date().toISOString() };
        newKeys.push(key);
    }

    saveLicenses(licenses);
    res.json({ keys: newKeys });
});

// ── Admin: Revoke a license ──────────────────────────────────────────────────
app.post("/admin/revoke-license", (req, res) => {
    const secret = req.headers["x-admin-secret"];
    if (!ADMIN_SECRET || secret !== ADMIN_SECRET) {
        return res.status(401).json({ error: "Unauthorized" });
    }

    const { key } = req.body;
    const licenses = loadLicenses();
    if (licenses[key]) {
        licenses[key].active = false;
        saveLicenses(licenses);
        res.json({ revoked: key });
    } else {
        res.status(404).json({ error: "License not found" });
    }
});

// ── Validate a license (called by plugin on startup) ─────────────────────────
app.post("/validate", (req, res) => {
    const { license_key } = req.body;
    if (!license_key) {
        return res.status(400).json({ valid: false, error: "license_key required" });
    }

    const license = validateLicense(license_key);
    if (!license) {
        return res.status(403).json({ valid: false, error: "Invalid or inactive license" });
    }

    res.json({ valid: true, tier: license.tier });
});

// ── Main chat endpoint ────────────────────────────────────────────────────────
app.post("/api/chat", async (req, res) => {
    const licenseKey = req.headers["x-license-key"];
    if (!licenseKey) {
        return res.status(401).json({ error: "x-license-key header required" });
    }

    const license = validateLicense(licenseKey);
    if (!license) {
        return res.status(403).json({ error: "Invalid or inactive license" });
    }

    const { messages, max_tokens = 1000 } = req.body;
    if (!messages || !Array.isArray(messages)) {
        return res.status(400).json({ error: "messages array required" });
    }

    // Separate system prompt from conversation
    const systemMessage = messages.find((m) => m.role === "system");
    const chatMessages = messages.filter((m) => m.role !== "system");
    const systemPrompt = systemMessage?.content ||
        "You are MixMind, an expert mixing and mastering assistant powered by real-time audio analysis.";

    // Set a timeout for the upstream API call
    const timeoutMs = 30_000;
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), timeoutMs);

    try {
        if (license.tier === "premium") {
            // ── Premium: Claude ──────────────────────────────────────────
            if (!anthropic) {
                return res.status(503).json({
                    error: "Premium tier is not configured on this server.",
                });
            }

            const response = await anthropic.messages.create({
                model: "claude-sonnet-4-20250514",
                max_tokens: Math.min(max_tokens, 4000),
                system: systemPrompt,
                messages: chatMessages,
            });

            clearTimeout(timeoutId);
            return res.json({
                content: response.content[0].text,
                model: "claude-sonnet-4-20250514",
                tier: "premium",
            });

        } else {
            // ── Standard: Groq / Llama ──────────────────────────────────
            if (!groq) {
                return res.status(503).json({
                    error: "Standard tier is not configured on this server.",
                });
            }

            const groqMessages = [
                { role: "system", content: systemPrompt },
                ...chatMessages,
            ];

            const response = await groq.chat.completions.create({
                model: "llama-3.3-70b-versatile",
                max_tokens: Math.min(max_tokens, 4000),
                messages: groqMessages,
            });

            clearTimeout(timeoutId);
            return res.json({
                content: response.choices[0].message.content,
                model: "llama-3.3-70b-versatile",
                tier: "standard",
            });
        }
    } catch (err) {
        clearTimeout(timeoutId);

        if (err.name === "AbortError") {
            console.error("API request timed out");
            return res.status(504).json({
                error: "The AI model did not respond in time. Please try a shorter question.",
            });
        }

        // Check for auth/permission errors
        if (err.status === 401 || err.status === 403) {
            console.error("API authentication error:", err.message);
            return res.status(502).json({
                error: "AI service authentication failed. Contact the server administrator.",
            });
        }

        // Rate limit from upstream
        if (err.status === 429) {
            console.error("Upstream rate limit hit:", err.message);
            return res.status(503).json({
                error: "AI service is temporarily rate-limited. Please wait and try again.",
            });
        }

        console.error("API error:", err.message || err);
        return res.status(500).json({
            error: "An unexpected error occurred. Please try again.",
        });
    }
});

// ── 404 catch-all ────────────────────────────────────────────────────────────
app.use((req, res) => {
    res.status(404).json({ error: `Not found: ${req.method} ${req.path}` });
});

// ── Global error handler ─────────────────────────────────────────────────────
app.use((err, req, res, _next) => {
    console.error("Unhandled error:", err);
    res.status(500).json({ error: "Internal server error" });
});

// ── Start ────────────────────────────────────────────────────────────────────
app.listen(PORT, () => {
    console.log(`MixMind proxy running on port ${PORT}`);
    console.log(`  Rate limit: ${RATE_LIMIT_MAX_REQ} req/min per IP`);
    console.log(`  Premium: ${ANTHROPIC_KEY ? "Claude (configured)" : "Claude (not configured)"}`);
    console.log(`  Standard: ${GROQ_KEY ? "Groq/Llama (configured)" : "Groq/Llama (not configured)"}`);
    console.log(`  Admin: ${ADMIN_SECRET ? "configured" : "not configured"}`);
    console.log(`  Licenses: ${Object.keys(loadLicenses()).length} on file`);
});
