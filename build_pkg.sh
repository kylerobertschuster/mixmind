#!/bin/bash
set -euo pipefail

# ─────────────────────────────────────────────────────────────────────────────
#  MixMind — Build, Codesign, Notarize & Package
#
#  Prerequisites:
#    1. Xcode 15+ with Command Line Tools installed
#    2. Apple Developer Program account with a "Developer ID Application"
#       certificate in your keychain
#
#  Usage:
#    ./build_pkg.sh                          # build + package (unsigned)
#    MIXMIND_SIGN_IDENTITY="..." ./build_pkg.sh  # build + sign + package
#    MIXMIND_NOTARIZE=true ./build_pkg.sh    # build + sign + notarize + package
#
#  Environment variables:
#    MIXMIND_SIGN_IDENTITY   Your Apple Developer ID cert name
#                            e.g. "Developer ID Application: Your Name (TEAMID)"
#    MIXMIND_NOTARIZE        Set to "true" to submit to Apple notarization
#    MIXMIND_BUNDLE_ID       Package identifier (default: com.mixmind.plugin)
# ─────────────────────────────────────────────────────────────────────────────

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
PKG_ROOT="${PROJECT_DIR}/pkg_root"
OUTPUT_PKG="${PROJECT_DIR}/MixMind-1.0.0.pkg"
BUNDLE_ID="${MIXMIND_BUNDLE_ID:-com.mixmind.plugin}"
SIGN_IDENTITY="${MIXMIND_SIGN_IDENTITY:-}"
DO_NOTARIZE="${MIXMIND_NOTARIZE:-false}"

echo "═══════════════════════════════════════════════════"
echo "  MixMind — Build & Package"
echo "═══════════════════════════════════════════════════"
echo "  Project:  ${PROJECT_DIR}"
echo "  Output:   ${OUTPUT_PKG}"
echo "  Bundle:   ${BUNDLE_ID}"
if [ -n "$SIGN_IDENTITY" ]; then
    echo "  Signing:  ${SIGN_IDENTITY}"
    echo "  Notarize: ${DO_NOTARIZE}"
else
    echo "  Signing:  (skipped — set MIXMIND_SIGN_IDENTITY to enable)"
fi
echo ""

# ═════════════════════════════════════════════════════════════════════════════
#  1. Clean previous build artifacts
# ═════════════════════════════════════════════════════════════════════════════
echo ">>> [1/6] Cleaning previous build..."
rm -rf "${BUILD_DIR}" "${PKG_ROOT}" "${OUTPUT_PKG}"

# ═════════════════════════════════════════════════════════════════════════════
#  2. Configure with CMake (FetchContent pulls JUCE automatically)
# ═════════════════════════════════════════════════════════════════════════════
echo ">>> [2/6] Configuring CMake..."
cmake -B "${BUILD_DIR}" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0 \
      -G "Unix Makefiles"

# ═════════════════════════════════════════════════════════════════════════════
#  3. Build all plugin formats
# ═════════════════════════════════════════════════════════════════════════════
echo ">>> [3/6] Building plugin..."
cmake --build "${BUILD_DIR}" --config Release --target MixMind_All -j "$(sysctl -n hw.ncpu)"

# ═════════════════════════════════════════════════════════════════════════════
#  4. Locate built binaries
# ═════════════════════════════════════════════════════════════════════════════
echo ">>> [4/6] Locating built plugins..."
ARTEFACTS="${BUILD_DIR}/MixMind_artefacts/Release"

VST3_SRC="${ARTEFACTS}/VST3/MixMind.vst3"
AU_SRC="${ARTEFACTS}/AU/MixMind.component"
STANDALONE_SRC="${ARTEFACTS}/Standalone/MixMind.app"

if [ ! -d "$VST3_SRC" ]; then
    echo "Error: VST3 not found at ${VST3_SRC}"
    ls "${ARTEFACTS}/" 2>/dev/null || true
    exit 1
fi
echo "  ✅ VST3:       ${VST3_SRC}"

if [ ! -d "$AU_SRC" ]; then
    echo "Warning: AU not found at ${AU_SRC} — skipping AU packaging"
    AU_SRC=""
else
    echo "  ✅ AU:         ${AU_SRC}"
fi

# ═════════════════════════════════════════════════════════════════════════════
#  5. Codesign (if identity is provided)
# ═════════════════════════════════════════════════════════════════════════════
if [ -n "$SIGN_IDENTITY" ]; then
    echo ">>> [5/6] Codesigning..."

    # VST3
    codesign --force --options runtime --sign "${SIGN_IDENTITY}" --verbose \
        "${VST3_SRC}"

    # AU (if present)
    if [ -n "$AU_SRC" ]; then
        codesign --force --options runtime --sign "${SIGN_IDENTITY}" --verbose \
            "${AU_SRC}"
    fi

    echo "  ✅ Codesigning complete"
else
    echo ">>> [5/6] Codesigning skipped (no MIXMIND_SIGN_IDENTITY)"
fi

# ═════════════════════════════════════════════════════════════════════════════
#  6. Package into .pkg installer
# ═════════════════════════════════════════════════════════════════════════════
echo ">>> [6/6] Creating package..."

mkdir -p "${PKG_ROOT}/Library/Audio/Plug-Ins/VST3"
cp -R "${VST3_SRC}" "${PKG_ROOT}/Library/Audio/Plug-Ins/VST3/"

if [ -n "$AU_SRC" ]; then
    mkdir -p "${PKG_ROOT}/Library/Audio/Plug-Ins/Components"
    cp -R "${AU_SRC}" "${PKG_ROOT}/Library/Audio/Plug-Ins/Components/"
fi

pkgbuild --root "${PKG_ROOT}" \
         --identifier "${BUNDLE_ID}" \
         --version 1.0.0 \
         --install-location "/" \
         "${OUTPUT_PKG}"

# Sign the package itself (if identity is provided)
if [ -n "$SIGN_IDENTITY" ]; then
    echo ">>> Signing package..."
    productsign --sign "${SIGN_IDENTITY}" "${OUTPUT_PKG}" "${OUTPUT_PKG}.signed"
    mv "${OUTPUT_PKG}.signed" "${OUTPUT_PKG}"
fi

echo ""
echo "═══════════════════════════════════════════════════"
echo "  ✅ Package created: ${OUTPUT_PKG}"
echo "═══════════════════════════════════════════════════"

# ═════════════════════════════════════════════════════════════════════════════
#  (Optional) Notarization
# ═════════════════════════════════════════════════════════════════════════════
if [ "$DO_NOTARIZE" = "true" ] && [ -n "$SIGN_IDENTITY" ]; then
    echo ""
    echo ">>> Submitting to Apple notarization..."
    echo "    (This may take a few minutes)..."

    # You must have configured your Apple ID credentials beforehand:
    #   xcrun notarytool store-credentials "mixmind" \
    #     --apple-id "your@email.com" \
    #     --team-id "YOUR_TEAM_ID" \
    #     --password "your-app-specific-password"

    xcrun notarytool submit "${OUTPUT_PKG}" \
        --keychain-profile "mixmind" \
        --wait

    echo ""
    echo ">>> Stapling notarization ticket..."
    xcrun stapler staple "${OUTPUT_PKG}"

    echo "  ✅ Notarization complete"
fi

echo ""
echo "═══════════════════════════════════════════════════"
echo "  Done! Share this file:"
echo "    ${OUTPUT_PKG}"
echo ""
echo "  Users can install by double-clicking the .pkg."
echo "  First-time setup required for notarization:"
echo "    xcrun notarytool store-credentials \"mixmind\" \\"
echo "      --apple-id \"your@email.com\" \\"
echo "      --team-id \"YOUR_TEAM_ID\" \\"
echo "      --password \"your-app-specific-password\""
echo "═══════════════════════════════════════════════════"
