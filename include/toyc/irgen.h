#pragma once

#include "toyc/ast.h"
#include "toyc/ast_access.h"
#include "toyc/ast_visitor.h"
#include "toyc/ir.h"
#include "toyc/ir_builder.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace toyc {

class DiagnosticEngine;

struct Symbol {
    Value* addr = nullptr;                 // alloca slot (local var/param) or global addr
    std::optional<int> const_value;        // set for const (local or global)
};

class IRGenerator : public ASTVisitor {
public:
    std::unique_ptr<Module> generate(const CompUnit& unit, DiagnosticEngine& diag);

    void visit_comp_unit(const CompUnit& unit) override;
    void visit_global_const(const GlobalConstDecl& decl) override;
    void visit_global_var(const GlobalVarDecl& decl) override;
    void visit_func_def(const FuncDef& func) override;

    void visit_block_stmt(const BlockStmt& node) override;
    void visit_empty_stmt(const EmptyStmt& node) override;
    void visit_expr_stmt(const ExprStmt& node) override;
    void visit_assign_stmt(const AssignStmt& node) override;
    void visit_const_decl_stmt(const ConstDeclStmt& node) override;
    void visit_var_decl_stmt(const VarDeclStmt& node) override;
    void visit_if_stmt(const IfStmt& node) override;
    void visit_while_stmt(const WhileStmt& node) override;
    void visit_break_stmt(const BreakStmt& node) override;
    void visit_continue_stmt(const ContinueStmt& node) override;
    void visit_return_stmt(const ReturnStmt& node) override;

    void visit_int_literal(const IntLiteralExpr& node) override;
    void visit_ident(const IdentExpr& node) override;
    void visit_binary(const BinaryExpr& node) override;
    void visit_unary(const UnaryExpr& node) override;
    void visit_call(const CallExpr& node) override;

private:
    Value* eval_expr(const Expr& expr);
    Value* short_circuit(bool is_and, const Expr& lhs, const Expr& rhs);
    std::optional<int> try_fold(const Expr& expr) const;

    void push_scope();
    void pop_scope();
    void declare(const std::string& name, Symbol sym);
    Symbol* resolve(const std::string& name);

    Value* alloca_in_entry();

    DiagnosticEngine* diag_ = nullptr;
    std::unique_ptr<Module> module_;
    std::unique_ptr<IRBuilder> builder_;
    BasicBlock* entry_ = nullptr;
    std::list<std::unique_ptr<Instruction>>::iterator alloca_pt_;
    FuncReturnType current_ret_ = FuncReturnType::Int;
    FuncSignatureMap func_sigs_;

    struct LoopFrame { BasicBlock* cond; BasicBlock* exit; };
    std::vector<LoopFrame> loops_;

    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    Value* last_value_ = nullptr;
    bool had_error_ = false;
};

std::unique_ptr<Module> generate(const CompUnit& unit, DiagnosticEngine& diag);

}  // namespace toyc
