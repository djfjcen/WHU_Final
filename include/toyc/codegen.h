#pragma once

#include <iosfwd>

namespace toyc {

class DiagnosticEngine;
class Module;

struct CodegenOptions {
    bool opt_mode = false;
    bool emit_exit_syscall = true;
    bool allow_pseudo = false;
};

bool emit_riscv(const Module& module, const CodegenOptions& options,
                DiagnosticEngine& diagnostics, std::ostream& out);

}  // namespace toyc
