#pragma once

#include <string>

namespace toyc::test {

// How far down the pipeline compile_ir() lowers ToyC source before printing.
enum class IrLevel {
    None,      // IRGen only (non-SSA, alloca/load/store)
    Mem2Reg,   // IRGen + mem2reg (SSA with phi)
    Optim,     // IRGen + mem2reg + run_optim
};

// Compile ToyC `source` to IR text by driving the in-process pipeline:
// Lexer -> Parser -> validate_comp_unit -> analyze -> generate -> [mem2reg] ->
// [run_optim] -> print_module. On any frontend/diagnostic error records a
// gtest failure (via ADD_FAILURE) and returns whatever was produced.
std::string compile_ir(const std::string& source, IrLevel level);

}  // namespace toyc::test
