#include "toyc/ast.h"
#include "toyc/ast_visitor.h"
#include "toyc/diagnostics.h"

namespace toyc {

std::unique_ptr<Expr> Expr::make_int_literal(int value, std::string lexeme, SourceLoc loc) {
    auto node = std::make_unique<Expr>();
    node->kind = Kind::IntLiteral;
    node->int_literal = IntLiteralExpr{value, std::move(lexeme), loc};
    return node;
}

std::unique_ptr<Expr> Expr::make_ident(std::string name, SourceLoc loc) {
    auto node = std::make_unique<Expr>();
    node->kind = Kind::Ident;
    node->ident = IdentExpr{std::move(name), loc};
    return node;
}

std::unique_ptr<Expr> Expr::make_binary(BinaryOp op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs,
                                        SourceLoc loc) {
    auto node = std::make_unique<Expr>();
    node->kind = Kind::Binary;
    node->binary = BinaryExpr{op, std::move(lhs), std::move(rhs), loc};
    return node;
}

std::unique_ptr<Expr> Expr::make_unary(UnaryOp op, std::unique_ptr<Expr> operand, SourceLoc loc) {
    auto node = std::make_unique<Expr>();
    node->kind = Kind::Unary;
    node->unary = UnaryExpr{op, std::move(operand), loc};
    return node;
}

std::unique_ptr<Expr> Expr::make_call(std::string callee, std::vector<std::unique_ptr<Expr>> args, SourceLoc loc) {
    auto node = std::make_unique<Expr>();
    node->kind = Kind::Call;
    node->call = CallExpr{std::move(callee), std::move(args), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_block(std::vector<std::unique_ptr<Stmt>> body, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Block;
    node->block = BlockStmt{std::move(body), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_empty(SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Empty;
    node->empty = EmptyStmt{loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_expr(std::unique_ptr<Expr> expr, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Expr;
    node->expr = ExprStmt{std::move(expr), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_assign(std::string name, std::unique_ptr<Expr> value, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Assign;
    node->assign = AssignStmt{std::move(name), std::move(value), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_const_decl(std::string name, std::unique_ptr<Expr> init, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::ConstDecl;
    node->const_decl = ConstDeclStmt{std::move(name), std::move(init), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_var_decl(std::string name, std::unique_ptr<Expr> init, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::VarDecl;
    node->var_decl = VarDeclStmt{std::move(name), std::move(init), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_if(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> then_branch,
                                    std::unique_ptr<Stmt> else_branch, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::If;
    node->if_stmt =
        IfStmt{std::move(condition), std::move(then_branch), std::move(else_branch), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_while(std::unique_ptr<Expr> condition, std::unique_ptr<Stmt> body, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::While;
    node->while_stmt = WhileStmt{std::move(condition), std::move(body), loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_break(SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Break;
    node->break_stmt = BreakStmt{loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_continue(SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Continue;
    node->continue_stmt = ContinueStmt{loc};
    return node;
}

std::unique_ptr<Stmt> Stmt::make_return(std::optional<std::unique_ptr<Expr>> value, SourceLoc loc) {
    auto node = std::make_unique<Stmt>();
    node->kind = Kind::Return;
    node->return_stmt = ReturnStmt{std::move(value), loc};
    return node;
}

const char* binary_op_name(BinaryOp op) {
    switch (op) {
        case BinaryOp::Or:
            return "||";
        case BinaryOp::And:
            return "&&";
        case BinaryOp::Lt:
            return "<";
        case BinaryOp::Le:
            return "<=";
        case BinaryOp::Gt:
            return ">";
        case BinaryOp::Ge:
            return ">=";
        case BinaryOp::Eq:
            return "==";
        case BinaryOp::Ne:
            return "!=";
        case BinaryOp::Add:
            return "+";
        case BinaryOp::Sub:
            return "-";
        case BinaryOp::Mul:
            return "*";
        case BinaryOp::Div:
            return "/";
        case BinaryOp::Mod:
            return "%";
    }
    return "?";
}

const char* unary_op_name(UnaryOp op) {
    switch (op) {
        case UnaryOp::Plus:
            return "+";
        case UnaryOp::Minus:
            return "-";
        case UnaryOp::Not:
            return "!";
    }
    return "?";
}

const char* func_return_type_name(FuncReturnType type) {
    switch (type) {
        case FuncReturnType::Int:
            return "int";
        case FuncReturnType::Void:
            return "void";
    }
    return "?";
}

namespace {

void require_expr(const std::unique_ptr<Expr>& expr, DiagnosticEngine& diagnostics, SourceLoc loc,
                  const char* context) {
    if (!expr) {
        diagnostics.error(DiagnosticStage::Ast, loc, std::string("missing expression in ") + context);
    }
}

class AstValidator : public ASTVisitor {
public:
    explicit AstValidator(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

    void visit_func_def(const FuncDef& func) override {
        if (!func.body) {
            diagnostics_.error(DiagnosticStage::Ast, func.loc,
                               "function '" + func.name + "' is missing a body");
        } else {
            ASTVisitor::visit_func_def(func);
        }
    }

private:
    DiagnosticEngine& diagnostics_;
};

}  // namespace

bool validate_comp_unit(const CompUnit& unit, DiagnosticEngine& diagnostics) {
    for (const CompUnit::Item& item : unit.items) {
        switch (item.kind) {
            case CompUnit::ItemKind::GlobalConst:
                require_expr(item.global_const.init, diagnostics, item.global_const.loc, "global const");
                break;
            case CompUnit::ItemKind::GlobalVar:
                require_expr(item.global_var.init, diagnostics, item.global_var.loc, "global var");
                break;
            case CompUnit::ItemKind::FuncDef:
                if (item.func_def.name.empty()) {
                    diagnostics.error(DiagnosticStage::Ast, item.func_def.loc, "function name is empty");
                }
                break;
        }
    }

    AstValidator validator(diagnostics);
    walk_comp_unit(unit, validator);
    return !diagnostics.has_errors();
}

}  // namespace toyc
