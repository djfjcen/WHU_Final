#include "toyc/compiler.h"

#include "toyc/ast_printer.h"
#include "toyc/codegen.h"
#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/ir_printer.h"
#include "toyc/irgen.h"
#include "toyc/lexer.h"
#include "toyc/mem2reg.h"
#include "toyc/optim.h"
#include "toyc/parser.h"
#include "toyc/sema.h"

#include <memory>

namespace toyc {
namespace {

std::unique_ptr<Module> build_ir(const CompUnit& unit,
                                 const CompilerOptions& options,
                                 DiagnosticEngine& diagnostics) {
    SemaResult sema = analyze(unit, diagnostics);
    if (diagnostics.has_errors() || !sema.ok) {
        return nullptr;
    }

    std::unique_ptr<Module> ir = generate(unit, sema, diagnostics);
    if (diagnostics.has_errors() || !ir) {
        return nullptr;
    }
    if (options.opt_mode || options.mem2reg_only) {
        if (options.opt_mode) {
            interprocedural_global_opt(*ir);
        }
        mem2reg(*ir);
    }
    if (options.opt_mode) {
        run_optim(*ir);
    }
    return ir;
}

int dump_tokens(Lexer& lexer, DiagnosticEngine& diagnostics,
                std::ostream& errors) {
    while (true) {
        const Token token = lexer.next_token();
        errors << token << '\n';
        if (token.type == TokenType::END_OF_FILE ||
            token.type == TokenType::INVALID) {
            break;
        }
    }
    if (lexer.has_error() || diagnostics.has_errors()) {
        diagnostics.emit_all(errors);
        return 1;
    }
    return 0;
}

}  // namespace

int run_compiler(const CompilerOptions& options, std::istream& input,
                 std::ostream& output, std::ostream& errors) {
    DiagnosticEngine diagnostics;
    Lexer lexer(input, diagnostics);

    if (options.dump_tokens) {
        return dump_tokens(lexer, diagnostics, errors);
    }

    Parser parser(lexer, diagnostics);
    std::unique_ptr<CompUnit> unit = parser.parse_comp_unit();
    if (diagnostics.has_errors() || parser.has_error() || !unit) {
        diagnostics.emit_all(errors);
        return 1;
    }

    if (!validate_comp_unit(*unit, diagnostics)) {
        diagnostics.emit_all(errors);
        return 1;
    }

    if (options.dump_ast) {
        dump_ast(errors, *unit);
        return 0;
    }

    std::unique_ptr<Module> ir = build_ir(*unit, options, diagnostics);
    if (!ir) {
        diagnostics.emit_all(errors);
        return 1;
    }

    if (options.dump_ir) {
        print_module(*ir, errors);
        return 0;
    }

    CodegenOptions cg_options;
    cg_options.opt_mode = options.opt_mode;
    if (!emit_riscv(*ir, cg_options, diagnostics, output)) {
        diagnostics.emit_all(errors);
        return 1;
    }
    return 0;
}

}  // namespace toyc
