#include "frontend_fixture.h"

#include "toyc/ast_visitor.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

namespace toyc {
namespace {

class RecordingVisitor final : public ASTVisitor {
public:
    std::vector<std::string> events;

    void visit_global_const(const GlobalConstDecl& decl) override {
        events.push_back("global_const:" + decl.name);
        ASTVisitor::visit_global_const(decl);
    }

    void visit_global_var(const GlobalVarDecl& decl) override {
        events.push_back("global_var:" + decl.name);
        ASTVisitor::visit_global_var(decl);
    }

    void visit_func_def(const FuncDef& func) override {
        events.push_back("func:" + func.name);
        ASTVisitor::visit_func_def(func);
    }

    void visit_int_literal(const IntLiteralExpr& node) override {
        events.push_back("int:" + node.lexeme);
    }

    void visit_ident(const IdentExpr& node) override {
        events.push_back("ident:" + node.name);
    }

    void visit_binary(const BinaryExpr& node) override {
        events.push_back(std::string("binary:") + binary_op_name(node.op));
        ASTVisitor::visit_binary(node);
    }

    void visit_unary(const UnaryExpr& node) override {
        events.push_back(std::string("unary:") + unary_op_name(node.op));
        ASTVisitor::visit_unary(node);
    }

    void visit_call(const CallExpr& node) override {
        events.push_back("call:" + node.callee);
        ASTVisitor::visit_call(node);
    }

    void visit_block_stmt(const BlockStmt& node) override {
        events.push_back("block");
        ASTVisitor::visit_block_stmt(node);
    }

    void visit_empty_stmt(const EmptyStmt&) override { events.push_back("empty"); }
    void visit_expr_stmt(const ExprStmt& node) override {
        events.push_back("expr_stmt");
        ASTVisitor::visit_expr_stmt(node);
    }
    void visit_assign_stmt(const AssignStmt& node) override {
        events.push_back("assign:" + node.name);
        ASTVisitor::visit_assign_stmt(node);
    }
    void visit_const_decl_stmt(const ConstDeclStmt& node) override {
        events.push_back("const_decl:" + node.name);
        ASTVisitor::visit_const_decl_stmt(node);
    }
    void visit_var_decl_stmt(const VarDeclStmt& node) override {
        events.push_back("var_decl:" + node.name);
        ASTVisitor::visit_var_decl_stmt(node);
    }
    void visit_if_stmt(const IfStmt& node) override {
        events.push_back("if");
        ASTVisitor::visit_if_stmt(node);
    }
    void visit_while_stmt(const WhileStmt& node) override {
        events.push_back("while");
        ASTVisitor::visit_while_stmt(node);
    }
    void visit_break_stmt(const BreakStmt&) override { events.push_back("break"); }
    void visit_continue_stmt(const ContinueStmt&) override { events.push_back("continue"); }
    void visit_return_stmt(const ReturnStmt& node) override {
        events.push_back("return");
        ASTVisitor::visit_return_stmt(node);
    }
};

TEST(ASTVisitor, DispatchesEveryNodeKind) {
    auto result = test::parse_source(
        "const int c = 1;\n"
        "int g = c;\n"
        "int main() {\n"
        "  const int lc = 2;\n"
        "  int v = +lc;\n"
        "  v = v * -lc;\n"
        "  foo(v);\n"
        "  ;\n"
        "  if (!v) break; else continue;\n"
        "  while (v) { return v; }\n"
        "}\n");
    ASSERT_NE(nullptr, result.unit);

    RecordingVisitor visitor;
    walk_comp_unit(*result.unit, visitor);

    const std::vector<std::string> required = {
        "global_const:c", "global_var:g", "func:main", "block",     "const_decl:lc",
        "var_decl:v",    "assign:v",      "expr_stmt", "empty",     "if",
        "break",         "continue",      "while",     "return",    "call:foo",
        "binary:*",      "unary:+",       "unary:-",   "unary:!",   "ident:v",
        "int:1",
    };
    for (const std::string& event : required) {
        EXPECT_NE(visitor.events.end(), std::find(visitor.events.begin(), visitor.events.end(), event))
            << event;
    }

    RecordingVisitor single_stmt;
    ASSERT_EQ(CompUnit::ItemKind::FuncDef, result.unit->items[2].kind);
    walk_stmt(test::only_stmt(result.unit->items[2].func_def, 0), single_stmt);
    EXPECT_FALSE(single_stmt.events.empty());

    RecordingVisitor single_expr;
    walk_expr(*result.unit->items[0].global_const.init, single_expr);
    EXPECT_EQ((std::vector<std::string>{"int:1"}), single_expr.events);
}

TEST(ASTVisitor, DefaultVisitorToleratesMissingOptionalChildren) {
    ASTVisitor visitor;
    GlobalConstDecl global_const{"c", nullptr, SourceLoc{}};
    GlobalVarDecl global_var{"v", nullptr, SourceLoc{}};
    FuncDef func;
    func.name = "f";
    visitor.visit_global_const(global_const);
    visitor.visit_global_var(global_var);
    visitor.visit_func_def(func);

    visitor.visit_int_literal(IntLiteralExpr{1, "1", SourceLoc{}});
    visitor.visit_ident(IdentExpr{"x", SourceLoc{}});
    visitor.visit_binary(BinaryExpr{BinaryOp::Add, nullptr, nullptr, SourceLoc{}});
    visitor.visit_unary(UnaryExpr{UnaryOp::Plus, nullptr, SourceLoc{}});
    std::vector<std::unique_ptr<Expr>> nullable_args;
    nullable_args.push_back(nullptr);
    visitor.visit_call(CallExpr{"f", std::move(nullable_args), SourceLoc{}});
    visitor.visit_call(CallExpr{"f", {}, SourceLoc{}});
    visitor.visit_empty_stmt(EmptyStmt{SourceLoc{}});
    visitor.visit_expr_stmt(ExprStmt{nullptr, SourceLoc{}});
    visitor.visit_assign_stmt(AssignStmt{"x", nullptr, SourceLoc{}});
    visitor.visit_const_decl_stmt(ConstDeclStmt{"c", nullptr, SourceLoc{}});
    visitor.visit_var_decl_stmt(VarDeclStmt{"v", nullptr, SourceLoc{}});
    visitor.visit_if_stmt(IfStmt{nullptr, nullptr, nullptr, SourceLoc{}});
    visitor.visit_while_stmt(WhileStmt{nullptr, nullptr, SourceLoc{}});
    visitor.visit_break_stmt(BreakStmt{SourceLoc{}});
    visitor.visit_continue_stmt(ContinueStmt{SourceLoc{}});
    visitor.visit_return_stmt(ReturnStmt{std::nullopt, SourceLoc{}});
    std::optional<std::unique_ptr<Expr>> null_value;
    null_value = nullptr;
    visitor.visit_return_stmt(ReturnStmt{std::move(null_value), SourceLoc{}});

    BlockStmt block;
    block.body.push_back(nullptr);
    visitor.visit_block_stmt(block);

    CompUnit invalid_unit;
    CompUnit::Item invalid_item;
    invalid_item.kind = static_cast<CompUnit::ItemKind>(999);
    invalid_unit.items.push_back(std::move(invalid_item));
    visitor.visit_comp_unit(invalid_unit);

    Stmt invalid_stmt;
    invalid_stmt.kind = static_cast<Stmt::Kind>(999);
    visitor.visit_stmt(invalid_stmt);

    Expr invalid_expr;
    invalid_expr.kind = static_cast<Expr::Kind>(999);
    visitor.visit_expr(invalid_expr);
}

}  // namespace
}  // namespace toyc
