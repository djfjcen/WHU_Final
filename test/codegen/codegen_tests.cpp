#include "toyc/codegen.h"

#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/ir.h"
#include "toyc/irgen.h"
#include "toyc/lexer.h"
#include "toyc/mem2reg.h"
#include "toyc/optim.h"
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

enum class CodegenPipeline {
    Raw,
    Mem2Reg,
    Optim,
};

Module make_return_const_module(int value) {
    Module module;
    Function* main = module.create_function("main", FuncRet::Int, 0);
    BasicBlock* entry = main->create_block();
    entry->push_back(std::make_unique<RetInst>(module.get_constant(value)));
    return module;
}

std::string compile_source_to_asm(const std::string& source,
                                  CodegenPipeline pipeline = CodegenPipeline::Raw) {
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

    if (pipeline == CodegenPipeline::Mem2Reg || pipeline == CodegenPipeline::Optim) {
        mem2reg(*module);
    }
    if (pipeline == CodegenPipeline::Optim) {
        run_optim(*module);
    }

    std::ostringstream out;
    CodegenOptions options;
    options.opt_mode = pipeline == CodegenPipeline::Optim;
    if (!emit_riscv(*module, options, diagnostics, out)) {
        std::ostringstream d;
        diagnostics.emit_all(d);
        ADD_FAILURE() << "codegen failed; diagnostics:\n" << d.str();
        return {};
    }
    return out.str();
}

void expect_stack_immediates_fit(const std::string& asm_text) {
    std::istringstream lines(asm_text);
    std::string line;
    while (std::getline(lines, line)) {
        const std::string addi_sp = "addi sp, sp, ";
        const std::size_t addi_pos = line.find(addi_sp);
        if (addi_pos != std::string::npos) {
            const int imm = std::stoi(line.substr(addi_pos + addi_sp.size()));
            EXPECT_TRUE(fits_i12(imm)) << line;
        }

        const std::size_t sp_addr = line.find("(sp)");
        if (sp_addr == std::string::npos) {
            continue;
        }
        const std::size_t comma = line.rfind(',', sp_addr);
        ASSERT_NE(std::string::npos, comma) << line;
        const int offset = std::stoi(line.substr(comma + 1, sp_addr - comma - 1));
        EXPECT_TRUE(fits_i12(offset)) << line;
    }
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
    EXPECT_NE(std::string::npos, asm_text.find("    blt t0, t1, .Lmain_"));
    EXPECT_NE(std::string::npos, asm_text.find("    beq t0, t1, .Lmain_"));
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

TEST(Codegen, CompilesOrFalseEdgeBeforeTrueEdgeCopy) {
    const std::string asm_text = compile_source_to_asm(
        "int g = 0; int side() { g = g + 1; return g; } "
        "int main() { if (0 || side()) { } return g; }\n");
    EXPECT_NE(std::string::npos,
              asm_text.find("    bne t0, t1, .Lmain_edge_entry_to_bb2\n"
                            "    j .Lmain_bb1\n"
                            ".Lmain_edge_entry_to_bb2:\n"));
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_bb1:\n    call side\n"));
}

TEST(Codegen, CompilesIfElseBothReturnWithoutDanglingMerge) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int a = 0; int b = 1; "
        "if (a && (b / a)) { return 1; } else { return 0; } }\n");
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
}

TEST(Codegen, CompilesLocalDeclarationAfterTerminatedBranch) {
    const std::string asm_text = compile_source_to_asm(
        "int f(int n) { if (n <= 1) { return 1; } int x = n + 2; return x; } "
        "int main() { return f(2); }\n");
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(std::string::npos, asm_text.find("f:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
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
    EXPECT_NE(std::string::npos, asm_text.find("    sw a0, 4(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t1, 144(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    call sum9\n"));
}

TEST(Codegen, CompilesLargeStackFrameWithoutIllegalImmediates) {
    std::ostringstream source;
    source << "int pick9(int a, int b, int c, int d, int e, int f, int g, int h, int i) {\n";
    for (int index = 0; index < 700; ++index) {
        source << "int v" << index << " = " << index << ";\n";
    }
    source << "return i + v699;\n";
    source << "}\n";
    source << "int main() { return pick9(1,2,3,4,5,6,7,8,9); }\n";

    const std::string asm_text = compile_source_to_asm(source.str());
    expect_stack_immediates_fit(asm_text);
    EXPECT_NE(std::string::npos, asm_text.find("    add sp, sp, t0\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    add t0, sp, t0\n"));
    EXPECT_EQ(std::string::npos, asm_text.find("2048(sp)"));
}

TEST(Codegen, CompilesMem2RegIfElsePhi) {
    const std::string asm_text = compile_source_to_asm(
        "int g = 1; int main() { int x = 0; if (g) { x = 1; } else { x = 2; } return x; }\n",
        CodegenPipeline::Mem2Reg);
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_edge_entry_to_bb1:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    j .Lmain_bb3\n"));
    EXPECT_EQ(std::string::npos, asm_text.find("codegen does not support phi"));
}

TEST(Codegen, CompilesMem2RegLoopHeaderPhi) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int i = 0; int sum = 0; while (i < 4) { "
        "sum = sum + i; i = i + 1; } return sum; }\n",
        CodegenPipeline::Mem2Reg);
    EXPECT_NE(std::string::npos, asm_text.find("    sw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    sw t0, 4(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t0, 0(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    lw t0, 4(sp)\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    j .Lmain_bb1\n"));
}

TEST(Codegen, CompilesOptimizedConstantBranch) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = 1; if (1) { x = 7; } else { x = 9; } return x; }\n",
        CodegenPipeline::Optim);
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_exit:\n"));
}

TEST(Codegen, CompilesOptimizedSharedValue) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int a = 3; int b = a + 4; int c = a + 4; return b + c; }\n",
        CodegenPipeline::Optim);
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    j .Lmain_exit\n"));
}

TEST(Codegen, CompilesOptimizedSampleShape) {
    std::ifstream input("test/sample.tc");
    ASSERT_TRUE(input.good());
    std::ostringstream source;
    source << input.rdbuf();
    const std::string asm_text = compile_source_to_asm(source.str(), CodegenPipeline::Optim);
    EXPECT_NE(std::string::npos, asm_text.find("    .section .text\n"));
    EXPECT_NE(std::string::npos, asm_text.find("    .globl main\n"));
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_exit:\n"));
}

TEST(Codegen, CompilesOptimizedLoopLocalDeclaration) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int i = 0; while (i < 2) { int j = 1; i = i + j; } return i; }\n",
        CodegenPipeline::Optim);
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
}

TEST(Codegen, CompilesOptimizedMergedPhiPredecessor) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = 0; int y = 0; "
        "if (!((0 > 0 && 1 < 0) || (2 > 0 && 0 < 0)) && (1 > 0 || 0 < 0)) { x = 1; } "
        "return x + y; }\n",
        CodegenPipeline::Optim);
    EXPECT_FALSE(asm_text.empty());
    EXPECT_NE(std::string::npos, asm_text.find("main:\n"));
}

TEST(Codegen, P7OptimizesFrameBranchesAndImmediates) {
    const std::string const_main = compile_source_to_asm(
        "int main() { return 15; }\n", CodegenPipeline::Optim);
    EXPECT_EQ(std::string::npos, const_main.find("addi sp, sp,"));

    const std::string leaf = compile_source_to_asm(
        "int f(int x) { int a = x + 1; int b = a * 2; return b + x; } "
        "int main() { return f(5); }\n",
        CodegenPipeline::Optim);
    EXPECT_NE(std::string::npos, leaf.find("f:\n"));
    EXPECT_EQ(std::string::npos, leaf.find("f:\n    sw ra,"));
    EXPECT_NE(std::string::npos, leaf.find("    addi t2, t0, 1\n"));
    EXPECT_NE(std::string::npos, leaf.find("    add t3, t2, x0\n"));
    EXPECT_EQ(std::string::npos, leaf.find("addi t0, t0, 0"));
}

TEST(Codegen, P7RemovesFallthroughJumpAfterDirectBranch) {
    const std::string asm_text = compile_source_to_asm(
        "int main() { int x = 0; while (x < 3) { x = x + 1; } return x; }\n");
    EXPECT_NE(std::string::npos, asm_text.find("    blt t0, t1, .Lmain_edge_bb1_to_bb2\n"));
    EXPECT_NE(std::string::npos, asm_text.find(".Lmain_edge_bb1_to_bb2:\n.Lmain_bb2:\n"));
    EXPECT_EQ(std::string::npos,
              asm_text.find(".Lmain_edge_bb1_to_bb2:\n    j .Lmain_bb2\n"));
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
