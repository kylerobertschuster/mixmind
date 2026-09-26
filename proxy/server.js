import 'dotenv/config';
import express from "express";
import { randomUUID } from "crypto";
import crypto from "crypto";
import fs from "fs";

// ─────────────────────────────────────────────────────────────────────────────
//  Config
// ─────────────────────────────────────────────────────────────────────────────
const PORT = process.env.PORT || 3000;
const LICENSES_FILE = process.env.LICENSES_FILE || "./licenses.json";
const ADMIN_SECRET = process.env.ADMIN_SECRET || "";

// Rate limiting: max requests per IP per window
const RATE_LIMIT_WINDOW_MS = 60_000;   // 1 minute
const RATE_LIMIT_MAX_REQ  = 60;         // 60 requests/min per IP

// ─────────────────────────────────────────────────────────────────────────────
//  Validate admin secret at startup
// ─────────────────────────────────────────────────────────────────────────────
if (!ADMIN_SECRET) {
    console.warn("⚠️  ADMIN_SECRET is not set. License admin endpoints are disabled.");
}

// ─────────────────────────────────────────────────────────────────────────────
//  License store
// ─────────────────────────────────────────────────────────────────────────────
let licensesCache = null;

function loadLicenses() {
    if (licensesCache === null) {
        if (!fs.existsSync(LICENSES_FILE)) fs.writeFileSync(LICENSES_FILE, "{}");
        licensesCache = JSON.parse(fs.readFileSync(LICENSES_FILE, "utf8"));
    }
    return licensesCache;
}

function saveLicenses(data) {
    licensesCache = data;
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
app.use(express.json({
    limit: "100kb",
    verify: (req, res, buf) => { req.rawBody = buf; },
}));
app.use(rateLimit);

// ── Health check (lightweight) ───────────────────────────────────────────────
app.get("/health", (req, res) => {
    res.json({
        status: "ok",
        uptime: process.uptime(),
        timestamp: new Date().toISOString(),
    });
});

// ── Health check (detailed) ──────────────────────────────────────────────────
// Liveness only. This used to report the model, whether the API/admin secrets
// were configured, and the live license counts — all public on the internet.
app.get("/healthz", (req, res) => {
    res.json({
        status: "ok",
        uptime: process.uptime(),
        timestamp: new Date().toISOString(),
        version: "2.1.0",
    });
});

// ── Admin: Generate license keys ─────────────────────────────────────────────
app.post("/admin/generate-license", (req, res) => {
    const secret = req.headers["x-admin-secret"];
    if (!ADMIN_SECRET || secret !== ADMIN_SECRET) {
        return res.status(401).json({ error: "Unauthorized" });
    }

    const { count = 1 } = req.body;
    const licenses = loadLicenses();
    const newKeys = [];

    for (let i = 0; i < count; i++) {
        const key = "MM-" + randomUUID().toUpperCase().replace(/-/g, "").substring(0, 16);
        licenses[key] = { active: true, createdAt: new Date().toISOString() };
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

    res.json({ valid: true });
});

// ── Purchase: gated behind Stripe webhook or admin key ────────────────────
// In production, /purchase is called ONLY by Stripe webhook with a secret.
// Until Stripe is configured, purchase requires x-admin-secret.
const STRIPE_WEBHOOK_SECRET = process.env.STRIPE_WEBHOOK_SECRET || "";

function verifyStripeSignature(req) {
    if (!STRIPE_WEBHOOK_SECRET) return false;
    const sig = req.headers["stripe-signature"];
    const raw = req.rawBody;
    if (!sig || !raw) return false;

    // Stripe sends: t=1234567890,v1=<hmac>,v0=<hmac> (comma-separated)
    const parts = Object.fromEntries(
        sig.split(",").map((pair) => {
            const idx = pair.indexOf("=");
            return [pair.slice(0, idx), pair.slice(idx + 1)];
        })
    );
    const timestamp = parts["t"];
    const provided = parts["v1"];
    if (!timestamp || !provided) return false;

    const signedPayload = `${timestamp}.${raw.toString("utf8")}`;
    const expected = crypto
        .createHmac("sha256", STRIPE_WEBHOOK_SECRET)
        .update(signedPayload, "utf8")
        .digest("hex");

    const a = Buffer.from(expected, "utf8");
    const b = Buffer.from(provided, "utf8");
    return a.length === b.length && crypto.timingSafeEqual(a, b);
}

app.post("/purchase", (req, res) => {
    // Verify the request is authorized before minting any license:
    //  - Stripe webhook (if configured): require a valid signature.
    //  - Otherwise: require the admin secret (manual license issuance).
    if (STRIPE_WEBHOOK_SECRET) {
        if (!verifyStripeSignature(req)) {
            return res.status(401).json({ error: "Invalid Stripe signature" });
        }
    } else {
        const secret = req.headers["x-admin-secret"];
        if (!ADMIN_SECRET || secret !== ADMIN_SECRET) {
            return res.status(402).json({
                error: "Payment required. Purchase flow is not yet configured.",
                hint: "Contact JuicePipe to purchase a license."
            });
        }
    }

    const { email } = req.body;
    if (!email || !email.includes("@")) {
        return res.status(400).json({ error: "Valid email required" });
    }
    const licenses = loadLicenses();
    const key = "MM-" + randomUUID().toUpperCase().replace(/-/g, "").substring(0, 16);
    licenses[key] = { active: true, createdAt: new Date().toISOString(), email };
    saveLicenses(licenses);
    console.log("Purchase: " + email + " -> " + key);
    res.json({ license_key: key, email });
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
    console.log(`JuicePipe proxy running on port ${PORT}`);
    console.log(`  Rate limit: ${RATE_LIMIT_MAX_REQ} req/min per IP`);
    console.log(`  Admin: ${ADMIN_SECRET ? "configured" : "not configured"}`);
    console.log(`  Licenses: ${Object.keys(loadLicenses()).length} on file`);
});
