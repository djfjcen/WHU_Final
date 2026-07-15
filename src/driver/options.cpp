#include "toyc/options.h"

#include <iostream>
#include <string>

namespace toyc {

void print_usage(std::ostream& output) {
    output << "Usage:\n"
           << "  toyc-compiler [options] < input.tc > output.s\n"
           << "Options:\n"
           << "  -dump-tokens         dump token stream to stderr\n"
           << "  -lex                 alias of -dump-tokens\n"
           << "  -dump-ast            dump AST to stderr\n"
           << "  -dump-ir             dump IR to stderr\n"
           << "  -dump-asm            alias of -dump-ir\n"
           << "  -opt                 enable IR and backend optimizations\n"
           << "  -mem2reg-only        run mem2reg only (test isolation)\n";
}

CompilerOptions parse_options(int argc, char* argv[], DiagnosticEngine& diagnostics) {
    CompilerOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-dump-tokens" || arg == "-lex") {
            options.dump_tokens = true;
        } else if (arg == "-dump-ast") {
            options.dump_ast = true;
        } else if (arg == "-dump-ir" || arg == "-dump-asm") {
            options.dump_ir = true;
        } else if (arg == "-opt") {
            options.opt_mode = true;
        } else if (arg == "-mem2reg-only") {
            options.mem2reg_only = true;
        } else {
            diagnostics.error(DiagnosticStage::Driver, SourceLoc{0, 0},
                              "unknown argument: " + arg);
            print_usage(std::cerr);
        }
    }
    return options;
}

}  // namespace toyc
