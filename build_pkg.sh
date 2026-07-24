#!/bin/bash
set -e

echo "====================================="
echo " Building MixMind VST3 and PKG..."
echo "====================================="

# Base directories
PROJECT_DIR="$(pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
PKG_ROOT="${PROJECT_DIR}/pkg_root"
OUTPUT_PKG="${PROJECT_DIR}/MixMind_Installer.pkg"

# 1. Build the project using CMake
echo ">>> Running CMake configure..."
cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
echo ">>> Building the plugin..."
cmake --build "${BUILD_DIR}" --config Release

# 2. Package the VST3 into a macOS .pkg installer
echo ">>> Preparing package root..."
# Clean previous packaging attempts
rm -rf "${PKG_ROOT}"
# Create the standard system VST3 directory structure for the package payload
mkdir -p "${PKG_ROOT}/Library/Audio/Plug-Ins/VST3"

# Find the VST3 bundle in the build folder and copy it to the payload directory
# JUCE typically outputs VST3 into build/MixMind_artefacts/Release/VST3/MixMind.vst3 
VST_PATH=$(find "${BUILD_DIR}" -name "MixMind.vst3" -type d | head -n 1)

if [ -z "$VST_PATH" ]; then
    echo "Error: Could not find MixMind.vst3 in the build folder."
    exit 1
fi

echo ">>> Copying $VST_PATH to package root..."
cp -R "$VST_PATH" "${PKG_ROOT}/Library/Audio/Plug-Ins/VST3/"

echo ">>> Building the .pkg installer..."
# Create the package installer. 
# The installer will unpack the contents of pkg_root to the target drive's root (/)
pkgbuild --root "${PKG_ROOT}" \
         --identifier com.yourstudio.mixmind \
         --version 1.0.0 \
         --install-location "/" \
         "${OUTPUT_PKG}"

echo "====================================="
echo " Done! Created ${OUTPUT_PKG}"
echo " You can share this .pkg file to install the VST3 on other Macs."
echo "====================================="
