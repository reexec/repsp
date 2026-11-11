#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
cd "${ROOT}"

CONFIG="${1:-Debug}"
PRESET="linux-x64-$(echo "${CONFIG}" | tr '[:upper:]' '[:lower:]')"

echo "[repsp] Configuring with preset ${PRESET}..."
cmake --preset "${PRESET}" -S "${ROOT}"

echo "[repsp] Building with preset ${PRESET}..."
cmake --build "build/${PRESET}"

echo "[repsp] Running tests..."
ctest --test-dir "build/${PRESET}" --output-on-failure

echo "[repsp] Linux build and tests succeeded."
