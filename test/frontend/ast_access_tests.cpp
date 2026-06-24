#include "frontend_fixture.h"

#include "toyc/ast_access.h"
#include "toyc/ast_contract.h"

#include <gtest/gtest.h>

namespace toyc {
namespace {

TEST(ASTAccess, CastHelpersReturnMatchingNodesOnly) {
    auto lit = Expr::make_int_literal(1, "1", SourceLoc{1, 1});
    auto ident = Expr::make_ident("x", SourceLoc{1, 3});
    auto binary = Expr::make_binary(BinaryOp::Add, Expr::make_int_literal(1, "1", SourceLoc{}),
                                    Expr::make_int_literal(2, "2", SourceLoc{}), SourceLoc{});
    auto unary = Expr::make_unary(UnaryOp::Minus, Expr::make_int_literal(1, "1", SourceLoc{}), SourceLoc{});
    std::vector<std::unique_ptr<Expr>> args;
    args.push_back(Expr::make_int_literal(1, "1", SourceLoc{}));
    auto call = Expr::make_call("f", std::move(args), SourceLoc{});

    EXPECT_NE(nullptr, as_int_literal(*lit));
    EXPECT_EQ(nullptr, as_ident(*lit));
    EXPECT_NE(nullptr, as_ident(*ident));
    EXPECT_EQ(nullptr, as_int_literal(*ident));
    EXPECT_NE(nullptr, as_binary(*binary));
    EXPECT_EQ(nullptr, as_binary(*lit));
    EXPECT_NE(nullptr, as_unary(*unary));
    EXPECT_EQ(nullptr, as_unary(*lit));
    EXPECT_NE(nullptr, as_call(*call));
    EXPECT_EQ(nullptr, as_call(*lit));

    auto block = Stmt::make_block({}, SourceLoc{});
    auto empty = Stmt::make_empty(SourceLoc{});
    auto assign = Stmt::make_assign("x", Expr::make_int_literal(1, "1", SourceLoc{}), SourceLoc{});
    auto constant = Stmt::make_const_decl("c", Expr::make_int_literal(1, "1", SourceLoc{}), SourceLoc{});
    auto variable = Stmt::make_var_decl("v", Expr::make_int_literal(1, "1", SourceLoc{}), SourceLoc{});
    EXPECT_NE(nullptr, as_block(*block));
    EXPECT_EQ(nullptr, as_block(*empty));
    EXPECT_NE(nullptr, as_assign(*assign));
    EXPECT_EQ(nullptr, as_assign(*empty));
    EXPECT_NE(nullptr, as_const_decl(*constant));
    EXPECT_EQ(nullptr, as_const_decl(*empty));
    EXPECT_NE(nullptr, as_var_decl(*variable));
    EXPECT_EQ(nullptr, as_var_decl(*empty));
}

TEST(ASTAccess, BuildsFunctionViewsAndNamesSharedEnums) {
    auto result = test::parse_source(
        "int f() { return 1; }\n"
        "const int c = 1;\n"
        "void g(int x) { return; }\n");
    ASSERT_NE(nullptr, result.unit);

    FuncSignatureMap signatures = build_func_signature_map(*result.unit);
    ASSERT_EQ(2U, signatures.size());
    EXPECT_EQ(FuncReturnType::Int, signatures.at("f"));
    EXPECT_EQ(FuncReturnType::Void, signatures.at("g"));

    std::vector<const FuncDef*> defs = collect_func_defs(*result.unit);
    ASSERT_EQ(2U, defs.size());
    EXPECT_EQ("f", defs[0]->name);
    EXPECT_EQ("g", defs[1]->name);

    EXPECT_STREQ("GlobalConst", symbol_storage_kind_name(SymbolStorageKind::GlobalConst));
    EXPECT_STREQ("GlobalVar", symbol_storage_kind_name(SymbolStorageKind::GlobalVar));
    EXPECT_STREQ("LocalConst", symbol_storage_kind_name(SymbolStorageKind::LocalConst));
    EXPECT_STREQ("LocalVar", symbol_storage_kind_name(SymbolStorageKind::LocalVar));
    EXPECT_STREQ("Param", symbol_storage_kind_name(SymbolStorageKind::Param));
    EXPECT_STREQ("int", value_type_name(ValueType::Int));
    EXPECT_STREQ("void", value_type_name(ValueType::Void));
    EXPECT_STREQ("Unknown", symbol_storage_kind_name(static_cast<SymbolStorageKind>(999)));
    EXPECT_STREQ("unknown", value_type_name(static_cast<ValueType>(999)));
}

}  // namespace
}  // namespace toyc
