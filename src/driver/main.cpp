#include "toyc/ast_printer.h"
#include "toyc/diagnostics.h"
#include "toyc/lexer.h"
#include "toyc/options.h"
#include "toyc/parser.h"

#include <iostream>
#include <memory>

namespace {

int dump_tokens(toyc::Lexer& lexer, toyc::DiagnosticEngine& diagnostics) {
    while (true) {
        const toyc::Token token = lexer.next_token();
        std::cerr << token << '\n';
        if (token.type == toyc::TokenType::END_OF_FILE || token.type == toyc::TokenType::INVALID) {
            break;
        }
    }
    if (lexer.has_error() || diagnostics.has_errors()) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }
    return 0;
}

int run_frontend(toyc::CompilerOptions options) {
    toyc::DiagnosticEngine diagnostics;
    toyc::Lexer lexer(std::cin, diagnostics);

    if (options.dump_tokens) {
        return dump_tokens(lexer, diagnostics);
    }

    toyc::Parser parser(lexer, diagnostics);
    std::unique_ptr<toyc::CompUnit> unit = parser.parse_comp_unit();
    if (diagnostics.has_errors() || parser.has_error() || !unit) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }

    if (!toyc::validate_comp_unit(*unit, diagnostics)) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }

    if (options.dump_ast) {
        toyc::dump_ast(std::cerr, *unit);
        return 0;
    }

    diagnostics.error(toyc::DiagnosticStage::Driver, toyc::SourceLoc{0, 0},
                      "codegen not implemented yet; use -dump-ast or -dump-tokens");
    diagnostics.emit_all(std::cerr);
    return 1;
}

}  // namespace

int main(int argc, char* argv[]) {
    toyc::DiagnosticEngine diagnostics;
    const toyc::CompilerOptions options = toyc::parse_options(argc, argv, diagnostics);
    if (diagnostics.has_errors()) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }

    (void)options.opt_mode;
    return run_frontend(options);
}
