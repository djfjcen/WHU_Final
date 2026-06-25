#include "frontend_fixture.h"

#include "toyc/ast_access.h"

#include <gtest/gtest.h>

namespace toyc {
namespace {

const Expr& require_binary(const Expr& expr, BinaryOp op) {
    EXPECT_EQ(Expr::Kind::Binary, expr.kind);
    EXPECT_EQ(op, expr.binary.op);
    return expr;
}

TEST(Parser, ParsesEmptyProgramGlobalsAndFunctionParameters) {
    auto empty = test::parse_source("");
    ASSERT_NE(nullptr, empty.unit);
    EXPECT_FALSE(empty.parser_had_error);
    EXPECT_TRUE(empty.unit->items.empty());

    auto result = test::parse_source(
        "const int answer = 42;\n"
        "int global = answer;\n"
        "void sink(int a, int b) { return; }\n");
    ASSERT_NE(nullptr, result.unit);
    EXPECT_FALSE(result.parser_had_error);
    ASSERT_EQ(3U, result.unit->items.size());

    EXPECT_EQ(CompUnit::ItemKind::GlobalConst, result.unit->items[0].kind);
    EXPECT_EQ("answer", result.unit->items[0].global_const.name);
    ASSERT_NE(nullptr, as_int_literal(*result.unit->items[0].global_const.init));

    EXPECT_EQ(CompUnit::ItemKind::GlobalVar, result.unit->items[1].kind);
    EXPECT_EQ("global", result.unit->items[1].global_var.name);
    ASSERT_NE(nullptr, as_ident(*result.unit->items[1].global_var.init));

    const FuncDef& func = result.unit->items[2].func_def;
    EXPECT_EQ(FuncReturnType::Void, func.return_type);
    EXPECT_EQ("sink", func.name);
    ASSERT_EQ(2U, func.params.size());
    EXPECT_EQ("a", func.params[0].name);
    EXPECT_EQ("b", func.params[1].name);
}

TEST(Parser, ParsesStatementKinds) {
    auto result = test::parse_source(
        "int main() {\n"
        "  {}\n"
        "  const int c = 1;\n"
        "  int v = c;\n"
        "  v = v + 1;\n"
        "  foo(v);\n"
        "  ;\n"
        "  if (v) break; else continue;\n"
        "  while (v) { return v; }\n"
        "}\n");
    ASSERT_NE(nullptr, result.unit);
    EXPECT_FALSE(result.parser_had_error);
    const FuncDef& func = test::only_function(*result.unit);
    ASSERT_EQ(8U, func.body->body.size());
    EXPECT_EQ(Stmt::Kind::Block, test::only_stmt(func, 0).kind);
    EXPECT_EQ(Stmt::Kind::ConstDecl, test::only_stmt(func, 1).kind);
    EXPECT_EQ(Stmt::Kind::VarDecl, test::only_stmt(func, 2).kind);
    EXPECT_EQ(Stmt::Kind::Assign, test::only_stmt(func, 3).kind);
    EXPECT_EQ(Stmt::Kind::Expr, test::only_stmt(func, 4).kind);
    EXPECT_EQ(Stmt::Kind::Empty, test::only_stmt(func, 5).kind);
    EXPECT_EQ(Stmt::Kind::If, test::only_stmt(func, 6).kind);
    EXPECT_EQ(Stmt::Kind::While, test::only_stmt(func, 7).kind);

    const IfStmt& if_stmt = test::only_stmt(func, 6).if_stmt;
    ASSERT_NE(nullptr, if_stmt.then_branch);
    ASSERT_NE(nullptr, if_stmt.else_branch);
    EXPECT_EQ(Stmt::Kind::Break, if_stmt.then_branch->kind);
    EXPECT_EQ(Stmt::Kind::Continue, if_stmt.else_branch->kind);
}

TEST(Parser, PreservesExpressionPrecedenceAndCalls) {
    auto result = test::parse_source("int main() { return +a * -b + !(c < d) == foo(1, 2) || e && f; }");
    ASSERT_NE(nullptr, result.unit);
    EXPECT_FALSE(result.parser_had_error);
    const Stmt& stmt = test::only_stmt(test::only_function(*result.unit));
    ASSERT_EQ(Stmt::Kind::Return, stmt.kind);
    ASSERT_TRUE(stmt.return_stmt.value);
    const Expr& root = **stmt.return_stmt.value;

    require_binary(root, BinaryOp::Or);
    require_binary(*root.binary.rhs, BinaryOp::And);
    require_binary(*root.binary.lhs, BinaryOp::Eq);

    const Expr& eq_lhs = *root.binary.lhs->binary.lhs;
    require_binary(eq_lhs, BinaryOp::Add);
    require_binary(*eq_lhs.binary.lhs, BinaryOp::Mul);
    ASSERT_EQ(Expr::Kind::Unary, eq_lhs.binary.lhs->binary.lhs->kind);
    EXPECT_EQ(UnaryOp::Plus, eq_lhs.binary.lhs->binary.lhs->unary.op);
    ASSERT_EQ(Expr::Kind::Unary, eq_lhs.binary.lhs->binary.rhs->kind);
    EXPECT_EQ(UnaryOp::Minus, eq_lhs.binary.lhs->binary.rhs->unary.op);
    ASSERT_EQ(Expr::Kind::Unary, eq_lhs.binary.rhs->kind);
    EXPECT_EQ(UnaryOp::Not, eq_lhs.binary.rhs->unary.op);
    require_binary(*eq_lhs.binary.rhs->unary.operand, BinaryOp::Lt);

    const Expr& call = *root.binary.lhs->binary.rhs;
    ASSERT_EQ(Expr::Kind::Call, call.kind);
    EXPECT_EQ("foo", call.call.callee);
    EXPECT_EQ(2U, call.call.args.size());
}

TEST(Parser, ParsesParenthesizedRelationalOperators) {
    auto result = test::parse_source("int main(){ return (1 <= 2) != (3 > 4) || (5 >= 6) / 7 % 8 - 9; }");
    ASSERT_NE(nullptr, result.unit);
    EXPECT_FALSE(result.parser_had_error);
    const Stmt& stmt = test::only_stmt(test::only_function(*result.unit));
    const Expr& root = **stmt.return_stmt.value;
    require_binary(root, BinaryOp::Or);
    require_binary(*root.binary.lhs, BinaryOp::Ne);
    require_binary(*root.binary.lhs->binary.lhs, BinaryOp::Le);
    require_binary(*root.binary.lhs->binary.rhs, BinaryOp::Gt);
    require_binary(*root.binary.rhs, BinaryOp::Sub);
    require_binary(*root.binary.rhs->binary.lhs, BinaryOp::Mod);
    require_binary(*root.binary.rhs->binary.lhs->binary.lhs, BinaryOp::Div);
    require_binary(*root.binary.rhs->binary.lhs->binary.lhs->binary.lhs, BinaryOp::Ge);
}

TEST(Parser, ParsesIdentAndLiteralExpressionStatements) {
    auto result = test::parse_source("int main(){ x; foo(); 1 + 2; if (x) return; }");
    ASSERT_NE(nullptr, result.unit);
    EXPECT_FALSE(result.parser_had_error);
    const FuncDef& func = test::only_function(*result.unit);
    ASSERT_EQ(4U, func.body->body.size());
    ASSERT_EQ(Stmt::Kind::Expr, func.body->body[0]->kind);
    ASSERT_EQ(Expr::Kind::Ident, func.body->body[0]->expr.expr->kind);
    ASSERT_EQ(Stmt::Kind::Expr, func.body->body[1]->kind);
    ASSERT_EQ(Expr::Kind::Call, func.body->body[1]->expr.expr->kind);
    EXPECT_TRUE(func.body->body[1]->expr.expr->call.args.empty());
    ASSERT_EQ(Stmt::Kind::Expr, func.body->body[2]->kind);
    require_binary(*func.body->body[2]->expr.expr, BinaryOp::Add);
    ASSERT_EQ(Stmt::Kind::If, func.body->body[3]->kind);
    EXPECT_EQ(nullptr, func.body->body[3]->if_stmt.else_branch);
}

TEST(Parser, ReportsRepresentativeErrors) {
    const char* sources[] = {
        "int main() { return 1 }",
        "int main( { return 1; }",
        "int main() { return 1;",
        "return 1;",
        "int main() { @; }",
        "int x;",
        "int main() { return *; }",
    };
    for (const char* source : sources) {
        auto result = test::parse_source(source);
        EXPECT_TRUE(result.parser_had_error || result.diagnostics.has_errors()) << source;
        EXPECT_TRUE(result.diagnostics.has_errors()) << source;
    }
}

TEST(Parser, StopsWhenLexerAlreadyFailed) {
    auto result = test::parse_source("@");
    EXPECT_EQ(nullptr, result.unit);
    EXPECT_TRUE(result.parser_had_error);
    EXPECT_TRUE(result.diagnostics.has_errors());
}

}  // namespace
}  // namespace toyc
