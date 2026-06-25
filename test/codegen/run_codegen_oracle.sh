#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
compiler="${repo_root}/build/toyc-compiler"
cases_dir="${repo_root}/test/codegen/cases"
work_dir="${TMPDIR:-/tmp}/toyc-codegen-oracle.$$"

find_tool() {
    local tool
    for tool in "$@"; do
        if command -v "${tool}" >/dev/null 2>&1; then
            command -v "${tool}"
            return 0
        fi
    done
    return 1
}

gcc_bin="$(find_tool riscv64-linux-gnu-gcc riscv32-linux-gnu-gcc || true)"
qemu_bin="$(find_tool qemu-riscv32 || true)"

if [[ ! -x "${compiler}" ]]; then
    echo "skip: build/toyc-compiler is missing; run just build first"
    exit 0
fi

if [[ -z "${gcc_bin}" || -z "${qemu_bin}" ]]; then
    echo "skip: requires riscv*-linux-gnu-gcc and qemu-riscv32"
    exit 0
fi

mkdir -p "${work_dir}"
trap 'rm -rf "${work_dir}"' EXIT

run_and_capture_status() {
    local bin="$1"
    set +e
    "${qemu_bin}" "${bin}" >/dev/null
    local status=$?
    set -e
    echo "${status}"
}

for source in "${cases_dir}"/*.tc; do
    name="$(basename "${source}" .tc)"
    asm_file="${work_dir}/${name}.s"
    toyc_bin="${work_dir}/${name}.toyc"
    gcc_bin_out="${work_dir}/${name}.gcc"

    "${compiler}" < "${source}" > "${asm_file}"
    "${gcc_bin}" -x assembler "${asm_file}" -o "${toyc_bin}" -march=rv32im -mabi=ilp32
    "${gcc_bin}" -x c "${source}" -o "${gcc_bin_out}" -march=rv32im -mabi=ilp32

    toyc_status="$(run_and_capture_status "${toyc_bin}")"
    gcc_status="$(run_and_capture_status "${gcc_bin_out}")"
    if [[ "${toyc_status}" != "${gcc_status}" ]]; then
        echo "FAIL ${name}: toyc exit ${toyc_status}, gcc exit ${gcc_status}"
        exit 1
    fi
    echo "ok ${name}: exit ${toyc_status}"
done
