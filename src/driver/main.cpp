#include "toyc/ast_printer.h"
#include "toyc/codegen.h"
#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/ir_printer.h"
#include "toyc/irgen.h"
#include "toyc/optim.h"
#include "toyc/mem2reg.h"
#include "toyc/lexer.h"
#include "toyc/options.h"
#include "toyc/parser.h"
#include "toyc/sema.h"

#include <iostream>
#include <memory>

namespace {

std::unique_ptr<toyc::Module> build_ir(const toyc::CompUnit& unit,
                                       const toyc::CompilerOptions& options,
                                       toyc::DiagnosticEngine& diagnostics) {
    toyc::SemaResult sema = toyc::analyze(unit, diagnostics);
    if (diagnostics.has_errors() || !sema.ok) {
        return nullptr;
    }
    std::unique_ptr<toyc::Module> ir = toyc::generate(unit, sema, diagnostics);
    if (diagnostics.has_errors() || !ir) {
        return nullptr;
    }
    if (options.opt_mode || options.mem2reg_only) {
        toyc::mem2reg(*ir);
    }
    if (options.opt_mode) {
        toyc::run_optim(*ir);
    }
    return ir;
}

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

    if (options.dump_ir) {
        std::unique_ptr<toyc::Module> ir = build_ir(*unit, options, diagnostics);
        if (!ir) {
            diagnostics.emit_all(std::cerr);
            return 1;
        }
        toyc::print_module(*ir, std::cerr);
        return 0;
    }

    std::unique_ptr<toyc::Module> ir = build_ir(*unit, options, diagnostics);
    if (!ir) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }
    toyc::CodegenOptions cg_options;
    cg_options.opt_mode = options.opt_mode;
    if (!toyc::emit_riscv(*ir, cg_options, diagnostics, std::cout)) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    toyc::DiagnosticEngine diagnostics;
    const toyc::CompilerOptions options = toyc::parse_options(argc, argv, diagnostics);
    if (diagnostics.has_errors()) {
        diagnostics.emit_all(std::cerr);
        return 1;
    }

    return run_frontend(options);
}
