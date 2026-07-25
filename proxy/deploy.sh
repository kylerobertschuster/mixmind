#!/bin/bash
set -euo pipefail

# ═══════════════════════════════════════════════════════════════════════
#  JuicePipe Proxy — One-Command Deploy
#  Run this on a fresh Ubuntu 24.04 VPS (BuyVM, Hetzner, etc.)
#  Usage:
#    curl -fsSL https://raw.githubusercontent.com/.../deploy.sh | bash
# ═══════════════════════════════════════════════════════════════════════

# ── Config ────────────────────────────────────────────────────────────
JUICEPIPE_DOMAIN="${JUICEPIPE_DOMAIN:-juicepipe.audio}"
JUICEPIPE_USER="${JUICEPIPE_USER:-juicepipe}"
JUICEPIPE_HOME="/home/${JUICEPIPE_USER}"

echo "═══════════════════════════════════════════════════"
echo "  JuicePipe Proxy — Deploy"
echo "═══════════════════════════════════════════════════"

# ── 1. System packages ───────────────────────────────────────────────
echo "[1/6] Installing system packages..."
apt-get update -qq
apt-get install -y -qq nginx certbot python3-certbot-nginx nodejs npm git

# ── 2. Create juicepipe user ─────────────────────────────────────────
echo "[2/6] Creating juicepipe user..."
id -u ${JUICEPIPE_USER} &>/dev/null || useradd -m -s /bin/bash ${JUICEPIPE_USER}

# ── 3. Clone and install proxy ───────────────────────────────────────
echo "[3/6] Installing proxy..."
if [ ! -d "${JUICEPIPE_HOME}/proxy" ]; then
    git clone --depth 1 https://github.com/kylerobertschuster/mixmind.git /tmp/mixmind
    cp -r /tmp/mixmind/proxy "${JUICEPIPE_HOME}/proxy"
    rm -rf /tmp/mixmind
fi
cd "${JUICEPIPE_HOME}/proxy"
npm install --production

# ── 4. Environment file ──────────────────────────────────────────────
echo "[4/6] Setting up environment..."
if [ ! -f "${JUICEPIPE_HOME}/proxy/.env" ]; then
    cat > "${JUICEPIPE_HOME}/proxy/.env" << 'EOF'
# ⚠️  EDIT THIS BEFORE STARTING THE PROXY
DEEPSEEK_API_KEY=your_deepseek_key_here
ADMIN_SECRET=change_this_to_a_random_secret
PORT=3000
EOF
    chown ${JUICEPIPE_USER}:${JUICEPIPE_USER} "${JUICEPIPE_HOME}/proxy/.env"
    chmod 600 "${JUICEPIPE_HOME}/proxy/.env"
    echo "  ⚠️  Run: nano ${JUICEPIPE_HOME}/proxy/.env"
    echo "  ⚠️  Set your DEEPSEEK_API_KEY and ADMIN_SECRET"
fi

# ── 5. PM2 process manager ──────────────────────────────────────────
echo "[5/6] Setting up PM2..."
npm install -g pm2
su - ${JUICEPIPE_USER} -c "cd ${JUICEPIPE_HOME}/proxy && pm2 start server.js --name juicepipe"
su - ${JUICEPIPE_USER} -c "pm2 save"
env PATH=$PATH:/usr/bin pm2 startup systemd -u ${JUICEPIPE_USER} --hp ${JUICEPIPE_HOME}

# ── 6. Nginx reverse proxy ──────────────────────────────────────────
echo "[6/6] Setting up nginx..."
cat > /etc/nginx/sites-available/juicepipe << NGINX
server {
    listen 80;
    server_name ${JUICEPIPE_DOMAIN} www.${JUICEPIPE_DOMAIN};

    location / {
        proxy_pass http://127.0.0.1:3000;
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection 'upgrade';
        proxy_set_header Host \$host;
        proxy_set_header X-Real-IP \$remote_addr;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
        proxy_read_timeout 30s;
    }
}
NGINX
ln -sf /etc/nginx/sites-available/juicepipe /etc/nginx/sites-enabled/
rm -f /etc/nginx/sites-enabled/default
nginx -t && systemctl restart nginx

echo ""
echo "═══════════════════════════════════════════════════"
echo "  ✅ Proxy deployed!"
echo ""
echo "  Configure DNS:"
echo "    Point ${JUICEPIPE_DOMAIN} → $(curl -4 -s ifconfig.me)"
echo ""
echo "  Then run:"
echo "    sudo certbot --nginx -d ${JUICEPIPE_DOMAIN}"
echo ""
echo "  Edit your API keys:"
echo "    sudo nano ${JUICEPIPE_HOME}/proxy/.env && sudo systemctl restart juicepipe"
echo ""
echo "  Generate a beta license:"
echo "    curl -X POST https://${JUICEPIPE_DOMAIN}/admin/generate-license \\"
echo "      -H \"x-admin-secret: YOUR_SECRET\" \\"
echo "      -d '{\"count\":10}'"
echo "═══════════════════════════════════════════════════"
