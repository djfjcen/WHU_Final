#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${TOYC_BUILD_DIR:-build-oracle}"
compiler="${repo_root}/${build_dir}/toyc-compiler"
work_dir="${TMPDIR:-/tmp}/toyc-perf.$$"
repeat="${TOYC_PERF_REPEAT:-5}"
gcc_bin="${RISCV_GCC:-riscv64-linux-gnu-gcc}"
qemu_bin="${QEMU_RISCV32:-qemu-riscv32}"

command -v "${gcc_bin}" >/dev/null
command -v "${qemu_bin}" >/dev/null
test -x "${compiler}"
mkdir -p "${work_dir}"
trap 'rm -rf "${work_dir}"' EXIT

start_file="${work_dir}/start.s"
printf '%s\n' '    .section .text' '    .globl _start' '_start:' \
  '    call main' '    li a7, 93' '    ecall' >"${start_file}"
flags=(-march=rv32im -mabi=ilp32 -nostdlib -static)

run_status() {
  set +e
  "${qemu_bin}" "$1" >/dev/null
  local status=$?
  set -e
  echo "${status}"
}

median_ns() {
  local binary="$1" samples=() begin end
  for ((i=0; i<repeat; ++i)); do
    begin="$(date +%s%N)"
    set +e
    "${qemu_bin}" "${binary}" >/dev/null
    set -e
    end="$(date +%s%N)"
    samples+=("$((end - begin))")
  done
  printf '%s\n' "${samples[@]}" | sort -n | sed -n "$(((repeat + 1) / 2))p"
}

printf '%-24s %10s %10s %10s %10s\n' case toyc_ns gcc_ns toyc_inst gcc_inst
for source in "${repo_root}"/test/perf/p*.tc; do
  name="$(basename "${source}" .tc)"
  toyc_asm="${work_dir}/${name}.toyc.s"
  gcc_asm="${work_dir}/${name}.gcc.s"
  toyc_bin="${work_dir}/${name}.toyc"
  gcc_out="${work_dir}/${name}.gcc"
  "${compiler}" -opt <"${source}" >"${toyc_asm}"
  "${gcc_bin}" -O2 -S -march=rv32im -mabi=ilp32 -x c "${source}" -o "${gcc_asm}"
  "${gcc_bin}" "${flags[@]}" -Wl,-e,main -x assembler "${toyc_asm}" -o "${toyc_bin}"
  "${gcc_bin}" "${flags[@]}" -Wl,-e,_start -x assembler "${start_file}" \
    -x c "${source}" -x none -o "${gcc_out}"
  test "$(run_status "${toyc_bin}")" = "$(run_status "${gcc_out}")"
  toyc_ns="$(median_ns "${toyc_bin}")"
  gcc_ns="$(median_ns "${gcc_out}")"
  toyc_inst="$(grep -Ec '^[[:space:]]+[a-z]' "${toyc_asm}" || true)"
  gcc_inst="$(grep -Ec '^[[:space:]]+[a-z]' "${gcc_asm}" || true)"
  printf '%-24s %10s %10s %10s %10s\n' \
    "${name}" "${toyc_ns}" "${gcc_ns}" "${toyc_inst}" "${gcc_inst}"
done
