#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
image="${TOYC_RISCV_ORACLE_IMAGE:-whu-compiler-riscv-oracle:latest}"

docker buildx build \
    --load \
    -f "${repo_root}/docker/riscv-oracle.Dockerfile" \
    -t "${image}" \
    "${repo_root}"

docker run --rm \
    -v "${repo_root}:/work" \
    -w /work \
    "${image}" \
    bash -lc 'cmake -S . -B build-oracle -DCMAKE_BUILD_TYPE=Debug && cmake --build build-oracle --target toyc-compiler && TOYC_BUILD_DIR=build-oracle test/codegen/run_codegen_oracle.sh'
