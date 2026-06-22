#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPILER="${1:-${ROOT}/../../../build/toyc-compiler}"

if [[ ! -x "$COMPILER" ]]; then
  COMPILER="${ROOT}/../../../build/Debug/toyc-compiler"
fi
if [[ ! -x "$COMPILER" && ! -f "${COMPILER}.exe" ]]; then
  COMPILER="${ROOT}/../../../build/Release/toyc-compiler.exe"
fi

cmake -DCOMPILER="$COMPILER" -P "${ROOT}/run_tests.cmake"
