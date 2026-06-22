#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace toyc {

struct SourceLoc {
    std::uint32_t line = 0;
    std::uint32_t column = 0;
};

enum class FuncReturnType { Int, Void };

enum class BinaryOp {
    Or,
    And,
    Lt,
    Le,
    Gt,
    Ge,
    Eq,
    Ne,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
};

enum class UnaryOp { Plus, Minus, Not };

struct Expr;
struct Stmt;

struct IntLiteralExpr {
    int value = 0;
    std::string lexeme;
    SourceLoc loc;
};

struct IdentExpr {
    std::string name;
    SourceLoc loc;
};

struct BinaryExpr {
    BinaryOp op = BinaryOp::Add;
    std::unique_ptr<Expr> lhs;
    std::unique_ptr<Expr> rhs;
    SourceLoc loc;
};

struct UnaryExpr {
    UnaryOp op = UnaryOp::Plus;
    std::unique_ptr<Expr> operand;
    SourceLoc loc;
};

struct CallExpr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    SourceLoc loc;
};

struct Expr {
    enum class Kind { IntLiteral, Ident, Binary, Unary, Call };

    Kind kind = Kind::IntLiteral;
    IntLiteralExpr int_literal;
    IdentExpr ident;
    BinaryExpr binary;
    UnaryExpr unary;
    CallExpr call;

    static std::unique_ptr<Expr> make_int_literal(int value, std::string lexeme, SourceLoc loc);
    static std::unique_ptr<Expr> make_ident(std::string name, SourceLoc loc);
    static std::unique_ptr<Expr> make_binary(BinaryOp op, std::unique_ptr<Expr> lhs,
                                             std::unique_ptr<Expr> rhs, SourceLoc loc);
    static std::unique_ptr<Expr> make_unary(UnaryOp op, std::unique_ptr<Expr> operand, SourceLoc loc);
    static std::unique_ptr<Expr> make_call(std::string callee, std::vector<std::unique_ptr<Expr>> args,
                                           SourceLoc loc);
};

struct BlockStmt {
    std::vector<std::unique_ptr<Stmt>> body;
    SourceLoc loc;
};

struct EmptyStmt {
    SourceLoc loc;
};

struct ExprStmt {
    std::unique_ptr<Expr> expr;
    SourceLoc loc;
};

struct AssignStmt {
    std::string name;
    std::unique_ptr<Expr> value;
    SourceLoc loc;
};

struct ConstDeclStmt {
    std::string name;
    std::unique_ptr<Expr> init;
    SourceLoc loc;
};

struct VarDeclStmt {
    std::string name;
    std::unique_ptr<Expr> init;
    SourceLoc loc;
};

struct IfStmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
    SourceLoc loc;
};

struct WhileStmt {
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> body;
    SourceLoc loc;
};

struct BreakStmt {
    SourceLoc loc;
};

struct ContinueStmt {
    SourceLoc loc;
};

struct ReturnStmt {
    std::optional<std::unique_ptr<Expr>> value;
    SourceLoc loc;
};

struct Stmt {
    enum class Kind {
        Block,
        Empty,
        Expr,
        Assign,
        ConstDecl,
        VarDecl,
        If,
        While,
        Break,
        Continue,
        Return,
    };

    Kind kind = Kind::Empty;
    BlockStmt block;
    EmptyStmt empty;
    ExprStmt expr;
    AssignStmt assign;
    ConstDeclStmt const_decl;
    VarDeclStmt var_decl;
    IfStmt if_stmt;
    WhileStmt while_stmt;
    BreakStmt break_stmt;
    ContinueStmt continue_stmt;
    ReturnStmt return_stmt;

    static std::unique_ptr<Stmt> make_block(std::vector<std::unique_ptr<Stmt>> body, SourceLoc loc);
    static std::unique_ptr<Stmt> make_empty(SourceLoc loc);
    static std::unique_ptr<Stmt> make_expr(std::unique_ptr<Expr> expr, SourceLoc loc);
    static std::unique_ptr<Stmt> make_assign(std::string name, std::unique_ptr<Expr> value, SourceLoc loc);
    static std::unique_ptr<Stmt> make_const_decl(std::string name, std::unique_ptr<Expr> init, SourceLoc loc);
    static std::unique_ptr<Stmt> make_var_decl(std::string name, std::unique_ptr<Expr> init, SourceLoc loc);
    static std::unique_ptr<Stmt> make_if(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> then_branch,
                                         std::unique_ptr<Stmt> else_branch, SourceLoc loc);
    static std::unique_ptr<Stmt> make_while(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body,
                                            SourceLoc loc);
    static std::unique_ptr<Stmt> make_break(SourceLoc loc);
    static std::unique_ptr<Stmt> make_continue(SourceLoc loc);
    static std::unique_ptr<Stmt> make_return(std::optional<std::unique_ptr<Expr>> value, SourceLoc loc);
};

struct Param {
    std::string name;
    SourceLoc loc;
};

struct FuncDef {
    FuncReturnType return_type = FuncReturnType::Int;
    std::string name;
    std::vector<Param> params;
    std::unique_ptr<BlockStmt> body;
    SourceLoc loc;
};

struct GlobalConstDecl {
    std::string name;
    std::unique_ptr<Expr> init;
    SourceLoc loc;
};

struct GlobalVarDecl {
    std::string name;
    std::unique_ptr<Expr> init;
    SourceLoc loc;
};

struct CompUnit {
    enum class ItemKind { GlobalConst, GlobalVar, FuncDef };

    struct Item {
        ItemKind kind = ItemKind::FuncDef;
        GlobalConstDecl global_const;
        GlobalVarDecl global_var;
        FuncDef func_def;
    };

    std::vector<Item> items;
};

const char* binary_op_name(BinaryOp op);
const char* unary_op_name(UnaryOp op);
const char* func_return_type_name(FuncReturnType type);

class DiagnosticEngine;
bool validate_comp_unit(const CompUnit& unit, DiagnosticEngine& diagnostics);

}  // namespace toyc
