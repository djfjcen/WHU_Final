#pragma once

#include "toyc/ast.h"
#include "toyc/diagnostics.h"
#include "toyc/lexer.h"

#include <memory>
#include <string>

namespace toyc {

class Parser {
public:
    Parser(Lexer& lexer, DiagnosticEngine& diagnostics);

    std::unique_ptr<CompUnit> parse_comp_unit();

    bool has_error() const { return had_error_; }

private:
    Lexer& lexer_;
    DiagnosticEngine& diagnostics_;
    Token current_;
    bool had_error_ = false;

    void advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    Token expect(TokenType type, const char* message);
    void report_error(const Token& token, const char* message);
    SourceLoc current_loc() const;

    std::unique_ptr<CompUnit> parse_comp_unit_impl();
    CompUnit::Item parse_top_level_item();
    GlobalConstDecl parse_global_const_decl();
    GlobalVarDecl parse_global_var_decl(const Token& name_token);
    FuncDef parse_func_def(FuncReturnType return_type, const Token& name_token);

    std::unique_ptr<Stmt> parse_stmt();
    std::unique_ptr<BlockStmt> parse_block();
    std::unique_ptr<Stmt> parse_decl_stmt();
    std::unique_ptr<Stmt> parse_var_decl_stmt(const Token& int_token, const Token& name_token);
    std::unique_ptr<Stmt> parse_if_stmt(const Token& if_token);
    std::unique_ptr<Stmt> parse_while_stmt(const Token& while_token);
    std::unique_ptr<Stmt> parse_return_stmt(const Token& return_token);

    std::unique_ptr<Expr> parse_expr();
    std::unique_ptr<Expr> parse_lor_expr();
    std::unique_ptr<Expr> parse_land_expr();
    std::unique_ptr<Expr> parse_rel_expr();
    std::unique_ptr<Expr> parse_add_expr();
    std::unique_ptr<Expr> parse_mul_expr();
    std::unique_ptr<Expr> parse_mul_expr_from_left(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_add_expr_from_left(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_rel_expr_from_left(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_land_expr_from_left(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_lor_expr_from_left(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parse_unary_expr();
    std::unique_ptr<Expr> parse_primary_expr();

    Param parse_param();
    std::vector<Param> parse_param_list();
    std::vector<std::unique_ptr<Expr>> parse_call_args();
    int parse_number_literal(const std::string& lexeme);
};

}  // namespace toyc
