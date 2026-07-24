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

const anthropic = new Anthropic({ apiKey: ANTHROPIC_KEY });
const groq = new Groq({ apiKey: GROQ_KEY });

// ─────────────────────────────────────────────────────────────────────────────
//  License store  (flat JSON file — swap for a DB later)
//  Format: { "LICENSE-KEY": { "tier": "standard" | "premium", "active": true } }
// ─────────────────────────────────────────────────────────────────────────────
function loadLicenses() {
    if (!fs.existsSync(LICENSES_FILE)) fs.writeFileSync(LICENSES_FILE, "{}");
    return JSON.parse(fs.readFileSync(LICENSES_FILE, "utf8"));
}

function saveLicenses(data) {
    fs.writeFileSync(LICENSES_FILE, JSON.stringify(data, null, 2));
}

function validateLicense(key) {
    const licenses = loadLicenses();
    const license = licenses[key];
    if (!license || !license.active) return null;
    return license; // { tier, active }
}

// ─────────────────────────────────────────────────────────────────────────────
//  App
// ─────────────────────────────────────────────────────────────────────────────
const app = express();
app.use(express.json());

// ── Health check ──────────────────────────────────────────────────────────────
app.get("/health", (req, res) => res.json({ status: "ok" }));

// ── Generate license keys (protect this route in production with a secret header)
app.post("/admin/generate-license", (req, res) => {
    const adminSecret = req.headers["x-admin-secret"];
    if (adminSecret !== process.env.ADMIN_SECRET) {
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

// ── Revoke a license ──────────────────────────────────────────────────────────
app.post("/admin/revoke-license", (req, res) => {
    const adminSecret = req.headers["x-admin-secret"];
    if (adminSecret !== process.env.ADMIN_SECRET) {
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
    if (!license_key) return res.status(400).json({ error: "license_key required" });

    const license = validateLicense(license_key);
    if (!license) return res.status(403).json({ valid: false, error: "Invalid or inactive license" });

    res.json({ valid: true, tier: license.tier });
});

// ── Main chat endpoint ────────────────────────────────────────────────────────
app.post("/api/chat", async (req, res) => {
    const licenseKey = req.headers["x-license-key"];
    if (!licenseKey) return res.status(401).json({ error: "x-license-key header required" });

    const license = validateLicense(licenseKey);
    if (!license) return res.status(403).json({ error: "Invalid or inactive license" });

    const { messages, model: requestedModel, max_tokens = 1000 } = req.body;
    if (!messages || !Array.isArray(messages)) {
        return res.status(400).json({ error: "messages array required" });
    }

    // Separate system prompt from conversation messages
    const systemMessage = messages.find(m => m.role === "system");
    const chatMessages = messages.filter(m => m.role !== "system");
    const systemPrompt = systemMessage?.content || "You are MixMind, an expert mixing assistant.";

    try {
        // Premium tier: use Claude. Standard tier: use Groq.
        if (license.tier === "premium") {
            const response = await anthropic.messages.create({
                model: "claude-sonnet-4-20250514",
                max_tokens,
                system: systemPrompt,
                messages: chatMessages,
            });

            return res.json({
                content: response.content[0].text,
                model: "claude-sonnet-4-20250514",
                tier: "premium",
            });

        } else {
            // Standard tier — Groq (Llama 3.3 70B is fast and capable)
            const groqMessages = [
                { role: "system", content: systemPrompt },
                ...chatMessages,
            ];

            const response = await groq.chat.completions.create({
                model: "llama-3.3-70b-versatile",
                max_tokens,
                messages: groqMessages,
            });

            return res.json({
                content: response.choices[0].message.content,
                model: "llama-3.3-70b-versatile",
                tier: "standard",
            });
        }

    } catch (err) {
        console.error("API error:", err);
        res.status(500).json({ error: err.message || "Internal server error" });
    }
});

app.listen(PORT, () => {
    console.log(`MixMind proxy running on port ${PORT}`);
    console.log(`Default tier: Groq (llama-3.3-70b-versatile)`);
    console.log(`Premium tier: Claude (claude-sonnet-4-20250514)`);
});
