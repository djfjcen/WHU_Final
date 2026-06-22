#pragma once

#include "toyc/ast.h"

namespace toyc {

class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit_comp_unit(const CompUnit& unit);
    virtual void visit_global_const(const GlobalConstDecl& decl);
    virtual void visit_global_var(const GlobalVarDecl& decl);
    virtual void visit_func_def(const FuncDef& func);

    virtual void visit_stmt(const Stmt& stmt);
    virtual void visit_expr(const Expr& expr);
};

void walk_comp_unit(const CompUnit& unit, ASTVisitor& visitor);
void walk_stmt(const Stmt& stmt, ASTVisitor& visitor);
void walk_block(const BlockStmt& block, ASTVisitor& visitor);
void walk_expr(const Expr& expr, ASTVisitor& visitor);

}  // namespace toyc
