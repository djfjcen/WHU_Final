#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/eval.h"
#include "toyc/lexer.h"
#include "toyc/parser.h"
#include "toyc/sema.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

namespace toyc {
namespace {

std::optional<std::int32_t> eval_source(const std::string& source,
                                        const EvalBudget& budget = {}) {
    DiagnosticEngine diagnostics;
    std::istringstream input(source);
    Lexer lexer(input, diagnostics);
    Parser parser(lexer, diagnostics);
    std::unique_ptr<CompUnit> unit = parser.parse_comp_unit();
    EXPECT_TRUE(unit != nullptr);
    EXPECT_FALSE(parser.has_error());
    EXPECT_TRUE(validate_comp_unit(*unit, diagnostics));
    SemaResult sema = analyze(*unit, diagnostics);
    EXPECT_TRUE(sema.ok);
    EXPECT_FALSE(diagnostics.has_errors());
    return evaluate_program(*unit, sema, budget);
}

TEST(Eval, ReturnsConstant) {
    auto value = eval_source("int main() { return 42; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(42, *value);
}

TEST(Eval, ArithmeticAndPrecedence) {
    auto value = eval_source("int main() { return 1 + 2 * 3 - 4 / 2; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(5, *value);
}

TEST(Eval, SignedDivisionTruncatesTowardZero) {
    auto value = eval_source("int main() { return (-7) / 2; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(-3, *value);
}

TEST(Eval, ModuloFollowsCSemantics) {
    auto value = eval_source("int main() { return (-7) % 2; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(-1, *value);
}

TEST(Eval, ShortCircuitAvoidsDivByZero) {
    auto value = eval_source("int main() { return 0 && (1 / 0); }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(0, *value);
}

TEST(Eval, GlobalVarMutationAcrossCalls) {
    auto value = eval_source(
        "int g = 0;\n"
        "void bump() { g = g + 5; }\n"
        "int main() { bump(); bump(); return g; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(10, *value);
}

TEST(Eval, GlobalConstChain) {
    auto value = eval_source(
        "const int a = 3;\n"
        "const int b = a * a + 1;\n"
        "int main() { return b; }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(10, *value);
}

TEST(Eval, LoopAccumulation) {
    auto value = eval_source(
        "int main() {\n"
        "  int i = 0; int acc = 0;\n"
        "  while (i < 1000) { acc = acc + i; i = i + 1; }\n"
        "  return acc;\n"
        "}\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(499500, *value);
}

TEST(Eval, BreakAndContinue) {
    auto value = eval_source(
        "int main() {\n"
        "  int i = 0; int acc = 0;\n"
        "  while (i < 100) {\n"
        "    i = i + 1;\n"
        "    if (i == 50) { break; }\n"
        "    if (i > 10) { continue; }\n"
        "    acc = acc + i;\n"
        "  }\n"
        "  return acc;\n"
        "}\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(55, *value);  // 1+2+...+10
}

TEST(Eval, RecursionWithMemoization) {
    auto value = eval_source(
        "int fib(int n) {\n"
        "  if (n < 2) { return n; }\n"
        "  return fib(n - 1) + fib(n - 2);\n"
        "}\n"
        "int main() { return fib(30); }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(832040, *value);
}

TEST(Eval, SelfTailRecursionRunsAsLoop) {
    auto value = eval_source(
        "int sum(int n, int acc) {\n"
        "  if (n == 0) { return acc; }\n"
        "  return sum(n - 1, acc + n);\n"
        "}\n"
        "int main() { return sum(100000, 0); }\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(705082704, *value);  // 100000*100001/2 wrapped to int32
}

TEST(Eval, IntegerOverflowWrapsAround) {
    auto value = eval_source(
        "int main() {\n"
        "  int x = 2147483647;\n"
        "  return x + 1;\n"
        "}\n");
    ASSERT_TRUE(value.has_value());
    EXPECT_EQ(static_cast<std::int32_t>(-2147483647 - 1), *value);
}

TEST(Eval, BudgetExhaustionFallsBack) {
    EvalBudget tiny;
    tiny.max_steps = 100;
    auto value = eval_source(
        "int main() {\n"
        "  int i = 0; int acc = 0;\n"
        "  while (i < 1000000) { acc = acc + i; i = i + 1; }\n"
        "  return acc;\n"
        "}\n",
        tiny);
    EXPECT_FALSE(value.has_value());
}

}  // namespace
}  // namespace toyc
