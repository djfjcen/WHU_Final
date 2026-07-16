#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${TOYC_BUILD_DIR:-build}"
compiler="${repo_root}/${build_dir}/toyc-compiler"
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
    echo "skip: ${build_dir}/toyc-compiler is missing; run just build first"
    exit 0
fi

if [[ -z "${gcc_bin}" || -z "${qemu_bin}" ]]; then
    echo "skip: requires riscv*-linux-gnu-gcc and qemu-riscv32"
    exit 0
fi

mkdir -p "${work_dir}"
trap 'rm -rf "${work_dir}"' EXIT

start_file="${work_dir}/start.s"
cat >"${start_file}" <<'ASM'
    .section .text
    .globl _start
_start:
    call main
    li a7, 93
    ecall
ASM

common_flags=(-march=rv32im -mabi=ilp32 -nostdlib -static)

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
    gcc_bin_out="${work_dir}/${name}.gcc"

    "${gcc_bin}" "${common_flags[@]}" -Wl,-e,_start -x assembler "${start_file}" \
        -x c "${source}" -x none -o "${gcc_bin_out}"
    gcc_status="$(run_and_capture_status "${gcc_bin_out}")"

    for mode in raw opt; do
        compiler_flags=()
        if [[ "${mode}" == opt ]]; then
            compiler_flags=(-opt)
        fi
        asm_file="${work_dir}/${name}.${mode}.s"
        toyc_bin="${work_dir}/${name}.${mode}.toyc"
        "${compiler}" "${compiler_flags[@]}" < "${source}" > "${asm_file}"
        "${gcc_bin}" "${common_flags[@]}" -Wl,-e,main -x assembler \
            "${asm_file}" -o "${toyc_bin}"
        toyc_status="$(run_and_capture_status "${toyc_bin}")"
        if [[ "${toyc_status}" != "${gcc_status}" ]]; then
            echo "FAIL ${name} (${mode}): toyc exit ${toyc_status}, gcc exit ${gcc_status}"
            exit 1
        fi
        echo "ok ${name} (${mode}): exit ${toyc_status}"
    done
done
