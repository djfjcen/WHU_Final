#include "toyc/ast_visitor.h"

namespace toyc {

void ASTVisitor::visit_comp_unit(const CompUnit& unit) {
    for (const CompUnit::Item& item : unit.items) {
        switch (item.kind) {
            case CompUnit::ItemKind::GlobalConst:
                visit_global_const(item.global_const);
                break;
            case CompUnit::ItemKind::GlobalVar:
                visit_global_var(item.global_var);
                break;
            case CompUnit::ItemKind::FuncDef:
                visit_func_def(item.func_def);
                break;
        }
    }
}

void ASTVisitor::visit_global_const(const GlobalConstDecl& decl) {
    if (decl.init) {
        visit_expr(*decl.init);
    }
}

void ASTVisitor::visit_global_var(const GlobalVarDecl& decl) {
    if (decl.init) {
        visit_expr(*decl.init);
    }
}

void ASTVisitor::visit_func_def(const FuncDef& func) {
    if (func.body) {
        walk_block(*func.body, *this);
    }
}

void ASTVisitor::visit_stmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case Stmt::Kind::Block:
            walk_block(stmt.block, *this);
            break;
        case Stmt::Kind::Empty:
            break;
        case Stmt::Kind::Expr:
            if (stmt.expr.expr) {
                visit_expr(*stmt.expr.expr);
            }
            break;
        case Stmt::Kind::Assign:
            if (stmt.assign.value) {
                visit_expr(*stmt.assign.value);
            }
            break;
        case Stmt::Kind::ConstDecl:
            if (stmt.const_decl.init) {
                visit_expr(*stmt.const_decl.init);
            }
            break;
        case Stmt::Kind::VarDecl:
            if (stmt.var_decl.init) {
                visit_expr(*stmt.var_decl.init);
            }
            break;
        case Stmt::Kind::If:
            if (stmt.if_stmt.condition) {
                visit_expr(*stmt.if_stmt.condition);
            }
            if (stmt.if_stmt.then_branch) {
                visit_stmt(*stmt.if_stmt.then_branch);
            }
            if (stmt.if_stmt.else_branch) {
                visit_stmt(*stmt.if_stmt.else_branch);
            }
            break;
        case Stmt::Kind::While:
            if (stmt.while_stmt.condition) {
                visit_expr(*stmt.while_stmt.condition);
            }
            if (stmt.while_stmt.body) {
                visit_stmt(*stmt.while_stmt.body);
            }
            break;
        case Stmt::Kind::Break:
        case Stmt::Kind::Continue:
            break;
        case Stmt::Kind::Return:
            if (stmt.return_stmt.value && *stmt.return_stmt.value) {
                visit_expr(**stmt.return_stmt.value);
            }
            break;
    }
}

void ASTVisitor::visit_expr(const Expr& expr) {
    switch (expr.kind) {
        case Expr::Kind::IntLiteral:
        case Expr::Kind::Ident:
            break;
        case Expr::Kind::Binary:
            if (expr.binary.lhs) {
                visit_expr(*expr.binary.lhs);
            }
            if (expr.binary.rhs) {
                visit_expr(*expr.binary.rhs);
            }
            break;
        case Expr::Kind::Unary:
            if (expr.unary.operand) {
                visit_expr(*expr.unary.operand);
            }
            break;
        case Expr::Kind::Call:
            for (const std::unique_ptr<Expr>& arg : expr.call.args) {
                if (arg) {
                    visit_expr(*arg);
                }
            }
            break;
    }
}

void walk_comp_unit(const CompUnit& unit, ASTVisitor& visitor) {
    visitor.visit_comp_unit(unit);
}

void walk_block(const BlockStmt& block, ASTVisitor& visitor) {
    for (const std::unique_ptr<Stmt>& stmt : block.body) {
        if (stmt) {
            visitor.visit_stmt(*stmt);
        }
    }
}

void walk_stmt(const Stmt& stmt, ASTVisitor& visitor) {
    visitor.visit_stmt(stmt);
}

void walk_expr(const Expr& expr, ASTVisitor& visitor) {
    visitor.visit_expr(expr);
}

}  // namespace toyc
