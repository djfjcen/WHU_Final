#include "toyc/compiler.h"
#include "toyc/diagnostics.h"
#include "toyc/options.h"

#include <iostream>

int main(int argc, char* argv[]) {
    toyc::DiagnosticEngine diagnostics;
    const toyc::CompilerOptions options =
        toyc::parse_options(argc, argv, diagnostics);
    if (diagnostics.has_errors()) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }

    return toyc::run_compiler(options, std::cin, std::cout, std::cerr);
}
