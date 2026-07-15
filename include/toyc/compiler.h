#pragma once

#include "toyc/options.h"

#include <iosfwd>

namespace toyc {

// Run the compiler pipeline used by the command-line driver.
//
// Every non-dump compilation follows the same source-to-assembly path:
// lexer -> parser -> AST validation -> semantic analysis -> IR generation ->
// optional IR optimization -> RISC-V code generation.
int run_compiler(const CompilerOptions& options, std::istream& input,
                 std::ostream& output, std::ostream& errors);

}  // namespace toyc
