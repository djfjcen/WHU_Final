#include "ir_fixture.h"

#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/ir_printer.h"
#include "toyc/irgen.h"
#include "toyc/lexer.h"
#include "toyc/mem2reg.h"
#include "toyc/optim.h"
#include "toyc/parser.h"
#include "toyc/sema.h"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>

namespace toyc::test {

std::string compile_ir(const std::string& source, IrLevel level) {
    DiagnosticEngine diagnostics;
    std::istringstream input(source);
    Lexer lexer(input, diagnostics);
    Parser parser(lexer, diagnostics);
    std::unique_ptr<CompUnit> unit = parser.parse_comp_unit();
    if (diagnostics.has_errors() || parser.has_error() || !unit) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "parse failed; diagnostics:\n" << d.str();
        return {};
    }

    if (!validate_comp_unit(*unit, diagnostics)) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "validate_comp_unit failed; diagnostics:\n" << d.str();
        return {};
    }

    SemaResult sema = analyze(*unit, diagnostics);
    if (diagnostics.has_errors() || !sema.ok) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "analyze failed; diagnostics:\n" << d.str();
        return {};
    }

    std::unique_ptr<Module> ir = generate(*unit, sema, diagnostics);
    if (diagnostics.has_errors() || !ir) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "generate failed; diagnostics:\n" << d.str();
        return {};
    }

    if (level == IrLevel::Mem2Reg || level == IrLevel::Optim) {
        mem2reg(*ir);
    }
    if (level == IrLevel::Optim) {
        run_optim(*ir);
    }

    std::ostringstream out;
    print_module(*ir, out);
    return out.str();
}

}  // namespace toyc::test
