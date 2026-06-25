#include "toyc/codegen.h"

#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/irgen.h"
#include "toyc/lexer.h"
#include "toyc/parser.h"
#include "toyc/riscv.h"
#include "toyc/sema.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace toyc {
namespace {

Module make_return_const_module(int value) {
    Module module;
    Function* main = module.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = main->create_block();
    entry->push_back(std::make_unique<RetInst>(module.get_constant(value)));
    return module;
}

std::string compile_source_to_asm(const std::string& source) {
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

    std::unique_ptr<Module> module = generate(*unit, sema, diagnostics);
    if (diagnostics.has_errors() || !module) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "IRGen failed; diagnostics:\n" << d.str();
        return {};
    }

    std::ostringstream out;
    CodegenOptions options;
    if (!emit_riscv(*module, options, diagnostics, out)) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "codegen failed; diagnostics:\n" << d.str();
        return {};
    }
    return out.str();
}

TEST(Codegen, EmitsReturnConstMain) {
    Module module = make_return_const_module(42);
    DiagnosticEngine diagnostics;
    std::ostringstream out;

    CodegenOptions options;
    EXPECT_TRUE(emit_riscv(module, options, diagnostics, out));
    EXPECT_FALSE(diagnostics.has_errors());

    const std::string asm_text = out.str();
    EXPECT_NE(std::string::npos, asm_text.find("    .section .text\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .globl main\n"));
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi a0, x0, 42\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi a7, x0, 93\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    ecall\n"));
}

TEST(Codegen, CompilesSourceReturnConstMain) {
    const std::string asm_text = compile_source_to_asm("int main() { return 42; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    .section .text\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .globl main\n"));
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi a0, x0, 42\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi a7, x0, 93\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    ecall\n"));
}

TEST(Codegen, CompilesLocalVarReturn) {
    const std::string asm_text = compile_source_to_asm("int main() { int x = 7; return x; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    addi sp, sp, -16\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi t1, x0, 7\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t1, 0(t0)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t1, 0(t0)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi sp, sp, 16\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    ecall\n"));
}

TEST(Codegen, CompilesLocalArithmetic) {
    const std::string asm_text =
        compile_source_to_asm("int main() { int x = -5; int y = 3000; return x + y * 2; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    addi t1, x0, -5\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lui t1, 1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi t1, t1, -1096\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    mul t2, t0, t1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    add t2, t0, t1\n"));
}

TEST(Codegen, EmitsGlobalsAndCompilesGlobalReadWrite) {
    const std::string asm_text =
        compile_source_to_asm("int g = 3; const int c = 4; int main() { g = g + c; return g; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    .section .rodata\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .globl c\n"));
    EXPECT_NE(std::string::npos, asm_text.find("c:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .word 4\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .section .data\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .globl g\n"));
    EXPECT_NE(std::string::npos, asm_text.find("g:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .word 3\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lui t0, %hi(g)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi t0, t0, %lo(g)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t1, 0(t0)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t1, 0(t0)\n"));
}

TEST(Codegen, CompilesLargeReturnConstantWithoutPseudo) {
    const std::string asm_text = compile_source_to_asm("int main() { return 1048577; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    lui a0, 256\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    addi a0, a0, 1\n"));
    EXPECT_EQ(std::string::npos, asm_text.find("    li "));
}

TEST(Codegen, RejectsUnsupportedMainBody) {
    Module module;
    Function* main = module.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = main->create_block();
    Value* sum = module.create_register(Type::I32);
    entry->push_back(std::make_unique<RetInst>(sum));

    DiagnosticEngine diagnostics;
    std::ostringstream out;
    CodegenOptions options;

    EXPECT_FALSE(emit_riscv(module, options, diagnostics, out));
    EXPECT_TRUE(diagnostics.has_errors());
    ASSERT_EQ(1u, diagnostics.diagnostics().size());
    EXPECT_EQ(DiagnosticStage::Codegen, diagnostics.diagnostics().front().stage);
    EXPECT_TRUE(out.str().empty());
}

TEST(Codegen, CompilesControlFlowAndComparisons) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = 0; while (x < 3) { x = x + 1; } "
        "if (x == 3) { return 7; } return 9; }\n");
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_bb1:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    slt t2, t0, t1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    xor t2, t0, t1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sltiu t2, t2, 1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    bne t0, x0, .Lmain_"));
    EXPECT_NE(std::string::npos, asm_text.find("    j .Lmain_exit\n"));
}

TEST(Codegen, CompilesDivRemAndShortCircuit) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = -9; int y = 4; if (x < 0 && y != 0) { "
        "return x / y + x % y; } return 0; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    div t2, t0, t1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    rem t2, t0, t1\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sltu t2, x0, t2\n"));
}

TEST(Codegen, CompilesRuntimeNegation) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = 9; return -x; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    sub t2, x0, t0\n"));
}

TEST(Codegen, CompilesFunctionCallsAndRecursion) {
    const std::string asm_text = compile_source_to_asm(
        "int fact(int n) { if (n <= 1) { return 1; } return n * fact(n - 1); } "
        "int main() { return fact(5); }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    .globl fact\n"));
    EXPECT_NE(std::string::npos, asm_text.find("fact:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw ra, "));
    EXPECT_NE(std::string::npos, asm_text.find("    call fact\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw ra, "));
    EXPECT_NE(std::string::npos, asm_text.find("    mul t2, t0, t1\n"));
}

TEST(Codegen, CompilesVoidCallMutatingGlobal) {
    const std::string asm_text = compile_source_to_asm(
        "int g = 0; void inc() { g = g + 1; return; } "
        "int main() { inc(); return g; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("inc:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    call inc\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lui t0, %hi(g)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t1, 0(t0)\n"));
}

TEST(Codegen, CompilesVoidFallthrough) {
    const std::string asm_text = compile_source_to_asm(
        "int g = 0; void set() { g = 1; } int main() { set(); return g; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("set:\n"));
    EXPECT_NE(std::string::npos, asm_text.find(".Lset_exit:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    call set\n"));
}

TEST(Codegen, CompilesMoreThanEightArgs) {
    const std::string asm_text = compile_source_to_asm(
        "int sum9(int a, int b, int c, int d, int e, int f, int g, int h, int i) { "
        "return a + b + c + d + e + f + g + h + i; } "
        "int main() { return sum9(1,2,3,4,5,6,7,8,9); }\n");
    EXPECT_NE(std::string::npos, asm_text.find("sum9:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t1, 112(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    call sum9\n"));
}

TEST(Codegen, CompilesEndToEndCases) {
    const std::vector<std::string> cases = {
        "return_const.tc",
        "local_arithmetic.tc",
        "if_else.tc",
        "while_sum.tc",
        "break_continue.tc",
        "global_mutation.tc",
        "call_recursive.tc",
        "void_call.tc",
        "many_args.tc",
    };

    for (const std::string& name : cases) {
        const std::filesystem::path path = std::filesystem::path("test/codegen/cases") / name;
        SCOPED_TRACE(path.string());
        std::ifstream input(path);
        ASSERT_TRUE(input.good());
        std::ostringstream source;
        source << input.rdbuf();
        const std::string asm_text = compile_source_to_asm(source.str());
        EXPECT_NE(std::string::npos, asm_text.find("    .section .text\n"));
        EXPECT_NE(std::string::npos, asm_text.find("    .globl main\n"));
        EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
        EXPECT_NE(std::string::npos, asm_text.find(".Lmain_exit:\n"));
    }
}

TEST(Riscv, RegisterNames) {
    EXPECT_STREQ("x0", reg_name(RvReg::Zero));
    EXPECT_STREQ("ra", reg_name(RvReg::Ra));
    EXPECT_STREQ("sp", reg_name(RvReg::Sp));
    EXPECT_STREQ("t0", reg_name(RvReg::T0));
    EXPECT_STREQ("s0", reg_name(RvReg::S0));
    EXPECT_STREQ("a0", reg_name(RvReg::A0));
    EXPECT_STREQ("a7", reg_name(RvReg::A7));
    EXPECT_STREQ("s11", reg_name(RvReg::S11));
    EXPECT_STREQ("t6", reg_name(RvReg::T6));
}

TEST(Riscv, ImmediateAndAlignmentHelpers) {
    EXPECT_TRUE(fits_i12(-2048));
    EXPECT_TRUE(fits_i12(2047));
    EXPECT_FALSE(fits_i12(-2049));
    EXPECT_FALSE(fits_i12(2048));

    EXPECT_EQ(0, align_to(0, 16));
    EXPECT_EQ(16, align_to(1, 16));
    EXPECT_EQ(16, align_to(16, 16));
    EXPECT_EQ(32, align_to(17, 16));
}

TEST(Riscv, LabelHelpers) {
    Module module;
    GlobalVar* global = module.create_global("g", 7, false);
    Function* function = module.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = function->create_block();
    BasicBlock* next = function->create_block();

    EXPECT_EQ("g", global_label(*global->addr));
    EXPECT_EQ("main", function_label(*function));
    EXPECT_EQ(".Lmain_entry", block_label(*function, *entry));
    EXPECT_EQ(".Lmain_bb1", block_label(*function, *next));
}

TEST(Riscv, AsmWriterFormatsText) {
    std::ostringstream out;
    AsmWriter writer(out);

    writer.section(".text");
    writer.global("main");
    writer.label("main");
    writer.inst("addi", "a0", "x0", "42");
    writer.inst("ecall");
    writer.comment("done");

    EXPECT_EQ("    .section .text\n"
              "    .globl main\n"
              "main:\n"
              "    addi a0, x0, 42\n"
              "    ecall\n"
              "    # done\n",
              out.str());
}

}  // namespace
}  // namespace toyc
