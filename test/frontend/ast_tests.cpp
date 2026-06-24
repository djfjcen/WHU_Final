#include "toyc/ast.h"
#include "toyc/diagnostics.h"

#include <gtest/gtest.h>

namespace toyc {
namespace {

std::unique_ptr<Expr> lit(int value) {
    return Expr::make_int_literal(value, std::to_string(value), SourceLoc{1, 2});
}

TEST(AST, ConstructsExpressionsAndNamesOperators) {
    auto int_lit = Expr::make_int_literal(7, "7", SourceLoc{1, 3});
    EXPECT_EQ(Expr::Kind::IntLiteral, int_lit->kind);
    EXPECT_EQ(7, int_lit->int_literal.value);
    EXPECT_EQ("7", int_lit->int_literal.lexeme);
    EXPECT_EQ(3U, int_lit->int_literal.loc.column);

    auto ident = Expr::make_ident("x", SourceLoc{2, 4});
    EXPECT_EQ(Expr::Kind::Ident, ident->kind);
    EXPECT_EQ("x", ident->ident.name);

    auto binary = Expr::make_binary(BinaryOp::Mod, lit(1), lit(2), SourceLoc{3, 5});
    EXPECT_EQ(Expr::Kind::Binary, binary->kind);
    EXPECT_EQ(BinaryOp::Mod, binary->binary.op);
    EXPECT_EQ(3U, binary->binary.loc.line);

    auto unary = Expr::make_unary(UnaryOp::Not, lit(0), SourceLoc{4, 6});
    EXPECT_EQ(Expr::Kind::Unary, unary->kind);
    EXPECT_EQ(UnaryOp::Not, unary->unary.op);

    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(lit(9));
    auto call = Expr::make_call("f", std::move(args), SourceLoc{5, 7});
    EXPECT_EQ(Expr::Kind::Call, call->kind);
    EXPECT_EQ("f", call->call.callee);
    EXPECT_EQ(1U, call->call.args.size());

    EXPECT_STREQ("||", binary_op_name(BinaryOp::Or));
    EXPECT_STREQ("&&", binary_op_name(BinaryOp::And));
    EXPECT_STREQ("<", binary_op_name(BinaryOp::Lt));
    EXPECT_STREQ("<=", binary_op_name(BinaryOp::Le));
    EXPECT_STREQ(">", binary_op_name(BinaryOp::Gt));
    EXPECT_STREQ(">=", binary_op_name(BinaryOp::Ge));
    EXPECT_STREQ("==", binary_op_name(BinaryOp::Eq));
    EXPECT_STREQ("!=", binary_op_name(BinaryOp::Ne));
    EXPECT_STREQ("+", binary_op_name(BinaryOp::Add));
    EXPECT_STREQ("-", binary_op_name(BinaryOp::Sub));
    EXPECT_STREQ("*", binary_op_name(BinaryOp::Mul));
    EXPECT_STREQ("/", binary_op_name(BinaryOp::Div));
    EXPECT_STREQ("%", binary_op_name(BinaryOp::Mod));
    EXPECT_STREQ("+", unary_op_name(UnaryOp::Plus));
    EXPECT_STREQ("-", unary_op_name(UnaryOp::Minus));
    EXPECT_STREQ("!", unary_op_name(UnaryOp::Not));
    EXPECT_STREQ("int", func_return_type_name(FuncReturnType::Int));
    EXPECT_STREQ("void", func_return_type_name(FuncReturnType::Void));
    EXPECT_STREQ("?", binary_op_name(static_cast<BinaryOp>(999)));
    EXPECT_STREQ("?", unary_op_name(static_cast<UnaryOp>(999)));
    EXPECT_STREQ("?", func_return_type_name(static_cast<FuncReturnType>(999)));
}

TEST(AST, ConstructsStatements) {
    std::vector<std::unique_ptr<Stmt>> body;
    body.push_back(Stmt::make_empty(SourceLoc{1, 1}));
    auto block = Stmt::make_block(std::move(body), SourceLoc{1, 2});
    EXPECT_EQ(Stmt::Kind::Block, block->kind);
    EXPECT_EQ(1U, block->block.body.size());

    EXPECT_EQ(Stmt::Kind::Expr, Stmt::make_expr(lit(1), SourceLoc{2, 1})->kind);
    EXPECT_EQ(Stmt::Kind::Assign, Stmt::make_assign("x", lit(2), SourceLoc{3, 1})->kind);
    EXPECT_EQ(Stmt::Kind::ConstDecl, Stmt::make_const_decl("c", lit(3), SourceLoc{4, 1})->kind);
    EXPECT_EQ(Stmt::Kind::VarDecl, Stmt::make_var_decl("v", lit(4), SourceLoc{5, 1})->kind);
    EXPECT_EQ(Stmt::Kind::If,
              Stmt::make_if(lit(1), Stmt::make_break(SourceLoc{6, 1}), nullptr, SourceLoc{6, 1})->kind);
    EXPECT_EQ(Stmt::Kind::While,
              Stmt::make_while(lit(1), Stmt::make_continue(SourceLoc{7, 1}), SourceLoc{7, 1})->kind);
    EXPECT_EQ(Stmt::Kind::Break, Stmt::make_break(SourceLoc{8, 1})->kind);
    EXPECT_EQ(Stmt::Kind::Continue, Stmt::make_continue(SourceLoc{9, 1})->kind);

    std::optional<std::unique_ptr<Expr>> value;
    value = lit(10);
    auto ret = Stmt::make_return(std::move(value), SourceLoc{10, 1});
    EXPECT_EQ(Stmt::Kind::Return, ret->kind);
    ASSERT_TRUE(ret->return_stmt.value);
}

TEST(AST, ValidatesRequiredFields) {
    DiagnosticEngine diagnostics;
    CompUnit unit;

    CompUnit::Item missing_const;
    missing_const.kind = CompUnit::ItemKind::GlobalConst;
    missing_const.global_const.name = "c";
    missing_const.global_const.loc = SourceLoc{1, 1};
    unit.items.push_back(std::move(missing_const));

    CompUnit::Item missing_var;
    missing_var.kind = CompUnit::ItemKind::GlobalVar;
    missing_var.global_var.name = "v";
    missing_var.global_var.loc = SourceLoc{2, 1};
    unit.items.push_back(std::move(missing_var));

    CompUnit::Item missing_func;
    missing_func.kind = CompUnit::ItemKind::FuncDef;
    missing_func.func_def.name = "";
    missing_func.func_def.loc = SourceLoc{3, 1};
    unit.items.push_back(std::move(missing_func));

    EXPECT_FALSE(validate_comp_unit(unit, diagnostics));
    EXPECT_TRUE(diagnostics.has_errors());
    EXPECT_EQ(4U, diagnostics.diagnostics().size());

    DiagnosticEngine ok_diagnostics;
    CompUnit valid;
    CompUnit::Item const_item;
    const_item.kind = CompUnit::ItemKind::GlobalConst;
    const_item.global_const.name = "c";
    const_item.global_const.init = Expr::make_int_literal(1, "1", SourceLoc{1, 1});
    valid.items.push_back(std::move(const_item));

    CompUnit::Item var_item;
    var_item.kind = CompUnit::ItemKind::GlobalVar;
    var_item.global_var.name = "v";
    var_item.global_var.init = Expr::make_int_literal(2, "2", SourceLoc{2, 1});
    valid.items.push_back(std::move(var_item));

    CompUnit::Item func_item;
    func_item.kind = CompUnit::ItemKind::FuncDef;
    func_item.func_def.name = "main";
    func_item.func_def.body = std::make_unique<BlockStmt>();
    valid.items.push_back(std::move(func_item));
    EXPECT_TRUE(validate_comp_unit(valid, ok_diagnostics));
    EXPECT_FALSE(ok_diagnostics.has_errors());

    DiagnosticEngine invalid_kind_diagnostics;
    CompUnit invalid_kind;
    CompUnit::Item invalid_item;
    invalid_item.kind = static_cast<CompUnit::ItemKind>(999);
    invalid_kind.items.push_back(std::move(invalid_item));
    EXPECT_TRUE(validate_comp_unit(invalid_kind, invalid_kind_diagnostics));
}

}  // namespace
}  // namespace toyc
