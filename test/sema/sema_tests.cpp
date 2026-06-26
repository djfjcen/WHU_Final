#include "toyc/ast.h"
#include "toyc/codegen.h"
#include "toyc/diagnostics.h"
#include "toyc/irgen.h"
#include "toyc/lexer.h"
#include "toyc/parser.h"
#include "toyc/sema.h"

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

namespace toyc {
namespace {

bool diagnostics_contain(const DiagnosticEngine& diagnostics, const std::string& needle) {
    for (const Diagnostic& diagnostic : diagnostics.diagnostics()) {
        if (diagnostic.message.find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

std::unique_ptr<CompUnit> parse_source(const std::string& source, DiagnosticEngine& diagnostics) {
    std::istringstream input(source);
    Lexer lexer(input, diagnostics);
    Parser parser(lexer, diagnostics);
    return parser.parse_comp_unit();
}

SemaResult analyze_source(const std::string& source, DiagnosticEngine& diagnostics) {
    std::unique_ptr<CompUnit> unit = parse_source(source, diagnostics);
    if (diagnostics.has_errors() || !unit) {
        return {};
    }
    if (!validate_comp_unit(*unit, diagnostics)) {
        return {};
    }
    return analyze(*unit, diagnostics);
}

std::string compile_source_to_asm(const std::string& source, DiagnosticEngine& diagnostics) {
    std::unique_ptr<CompUnit> unit = parse_source(source, diagnostics);
    if (diagnostics.has_errors() || !unit) {
        return {};
    }
    if (!validate_comp_unit(*unit, diagnostics)) {
        return {};
    }

    SemaResult sema = analyze(*unit, diagnostics);
    if (diagnostics.has_errors() || !sema.ok) {
        return {};
    }

    std::unique_ptr<Module> module = generate(*unit, sema, diagnostics);
    if (diagnostics.has_errors() || !module) {
        return {};
    }

    std::ostringstream out;
    CodegenOptions options;
    if (!emit_riscv(*module, options, diagnostics, out)) {
        return {};
    }
    return out.str();
}

void expect_short_circuit_const_ok(const std::string& source) {
    DiagnosticEngine diagnostics;
    SemaResult sema = analyze_source(source, diagnostics);

    EXPECT_TRUE(sema.ok) << "sema failed; diagnostics:\n";
    if (!sema.ok || diagnostics.has_errors()) {
        std::ostringstream emitted;
        diagnostics.emit_all(emitted);
        ADD_FAILURE() << emitted.str();
    }
    EXPECT_FALSE(diagnostics_contain(diagnostics, "division by zero in compile-time constant expression"));
    EXPECT_FALSE(diagnostics_contain(diagnostics, "modulo by zero in compile-time constant expression"));

    diagnostics.clear();
    const std::string asm_text = compile_source_to_asm(source, diagnostics);
    EXPECT_FALSE(asm_text.empty()) << "codegen failed; diagnostics:\n";
    if (asm_text.empty() || diagnostics.has_errors()) {
        std::ostringstream emitted;
        diagnostics.emit_all(emitted);
        ADD_FAILURE() << emitted.str();
    }
    EXPECT_FALSE(diagnostics_contain(diagnostics, "division by zero in compile-time constant expression"));
    EXPECT_FALSE(diagnostics_contain(diagnostics, "modulo by zero in compile-time constant expression"));
}

TEST(Sema, ShortCircuitConstAndSkipsDeadDivByZero) {
    expect_short_circuit_const_ok(R"(
const int c = 0 && (1 / 0);
int main() {
    return c;
}
)");
}

TEST(Sema, ShortCircuitConstOrSkipsDeadDivByZero) {
    expect_short_circuit_const_ok(R"(
const int c = 1 || (1 / 0);
int main() {
    return c;
}
)");
}

}  // namespace
}  // namespace toyc
