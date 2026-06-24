#include "frontend_fixture.h"

#include "toyc/ast_printer.h"

#include <gtest/gtest.h>

#include <sstream>

namespace toyc {
namespace {

TEST(ASTPrinter, DumpsCompUnitGoldenText) {
    auto result = test::parse_source(
        "const int c = 1;\n"
        "int g = c;\n"
        "int main(int x) {\n"
        "  int v = x;\n"
        "  if (v) { return foo(v, 2); } else return 0;\n"
        "}\n");
    ASSERT_NE(nullptr, result.unit);

    const std::string expected =
        "CompUnit\n"
        "  GlobalConst(c) @1:11\n"
        "    IntLiteral(1) @1:15\n"
        "\n"
        "  GlobalVar(g) @2:6\n"
        "    Ident(c) @2:10\n"
        "\n"
        "  FuncDef int main(int x) @3:6\n"
        "      VarDecl(v) @4:8\n"
        "        Ident(x) @4:12\n"
        "\n"
        "      If @5:4\n"
        "        Cond:\n"
        "          Ident(v) @5:8\n"
        "\n"
        "        Then:\n"
        "          Block @5:11\n"
        "            Return @5:13\n"
        "              Call(foo) @5:20\n"
        "                Ident(v) @5:24\n"
        "\n"
        "                IntLiteral(2) @5:27\n"
        "\n"
        "\n"
        "\n"
        "        Else:\n"
        "          Return @5:38\n"
        "            IntLiteral(0) @5:45\n"
        "\n"
        "\n";
    EXPECT_EQ(expected, test::dump_ast_text(*result.unit));
}

TEST(ASTPrinter, PrintsIndividualStatementsAndExpressions) {
    auto result = test::parse_source("int main(){ while (a < b) { a = a + 1; continue; } return; }");
    ASSERT_NE(nullptr, result.unit);
    const FuncDef& func = test::only_function(*result.unit);

    std::ostringstream stmt_out;
    ASTPrinter printer(stmt_out);
    printer.print(test::only_stmt(func, 0));
    EXPECT_EQ(
        "While @1:13\n"
        "  Cond:\n"
        "    Binary(<) @1:24\n"
        "      Ident(a) @1:20\n"
        "\n"
        "      Ident(b) @1:24\n"
        "\n"
        "  Body:\n"
        "    Block @1:27\n"
        "      Assign(a) @1:29\n"
        "        Binary(+) @1:37\n"
        "          Ident(a) @1:33\n"
        "\n"
        "          IntLiteral(1) @1:37\n"
        "\n"
        "      Continue @1:40\n"
        "\n"
        "\n",
        stmt_out.str());

    std::ostringstream expr_out;
    ASTPrinter expr_printer(expr_out);
    expr_printer.print(*Expr::make_unary(UnaryOp::Minus, Expr::make_int_literal(1, "1", SourceLoc{9, 4}),
                                         SourceLoc{9, 3}));
    EXPECT_EQ("Unary(-) @9:3\n  IntLiteral(1) @9:4\n", expr_out.str());
}

TEST(ASTPrinter, PrintsRemainingStatementAndParameterForms) {
    std::ostringstream stmt_out;
    ASTPrinter printer(stmt_out);
    printer.print(*Stmt::make_empty(SourceLoc{1, 1}));
    printer.print(*Stmt::make_expr(Expr::make_ident("x", SourceLoc{2, 3}), SourceLoc{2, 1}));
    printer.print(*Stmt::make_const_decl("c", Expr::make_int_literal(1, "1", SourceLoc{3, 5}),
                                         SourceLoc{3, 1}));
    printer.print(*Stmt::make_break(SourceLoc{4, 1}));
    printer.print(*Stmt::make_if(Expr::make_ident("ok", SourceLoc{5, 5}),
                                 Stmt::make_return(std::nullopt, SourceLoc{5, 9}), nullptr,
                                 SourceLoc{5, 1}));
    printer.print(*Expr::make_call("zero", {}, SourceLoc{6, 1}));
    EXPECT_EQ(
        "EmptyStmt @1:1\n"
        "ExprStmt @2:1\n"
        "  Ident(x) @2:3\n"
        "ConstDecl(c) @3:1\n"
        "  IntLiteral(1) @3:5\n"
        "Break @4:1\n"
        "If @5:1\n"
        "  Cond:\n"
        "    Ident(ok) @5:5\n"
        "\n"
        "  Then:\n"
        "    Return @5:9\n"
        "Call(zero) @6:1\n",
        stmt_out.str());

    CompUnit unit;
    CompUnit::Item item;
    item.kind = CompUnit::ItemKind::FuncDef;
    item.func_def.return_type = FuncReturnType::Void;
    item.func_def.name = "pair";
    item.func_def.params = {Param{"a", SourceLoc{1, 15}}, Param{"b", SourceLoc{1, 22}}};
    item.func_def.body = std::make_unique<BlockStmt>();
    item.func_def.body->loc = SourceLoc{1, 25};
    item.func_def.loc = SourceLoc{1, 6};
    unit.items.push_back(std::move(item));

    std::ostringstream unit_out;
    dump_ast(unit_out, unit);
    EXPECT_EQ("CompUnit\n  FuncDef void pair(int a, int b) @1:6\n", unit_out.str());

    Expr invalid_expr;
    invalid_expr.kind = static_cast<Expr::Kind>(999);
    Stmt invalid_stmt;
    invalid_stmt.kind = static_cast<Stmt::Kind>(999);
    CompUnit invalid_unit;
    CompUnit::Item invalid_item;
    invalid_item.kind = static_cast<CompUnit::ItemKind>(999);
    invalid_unit.items.push_back(std::move(invalid_item));

    std::ostringstream invalid_out;
    ASTPrinter invalid_printer(invalid_out);
    invalid_printer.print(invalid_expr);
    invalid_printer.print(invalid_stmt);
    invalid_printer.print(invalid_unit);
    EXPECT_EQ("\n\nCompUnit\n", invalid_out.str());
}

}  // namespace
}  // namespace toyc
