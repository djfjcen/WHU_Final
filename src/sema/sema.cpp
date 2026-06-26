#include "toyc/sema.h"

#include "toyc/ast_access.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace toyc {

namespace {

enum class SymbolKind { Object, Function };

struct Symbol {
    SymbolKind kind = SymbolKind::Object;
    SymbolRef ref;
    std::optional<int> const_value;
    const FuncDef* func = nullptr;
};

struct StmtFlow {
    bool returns = false;
};

class Analyzer {
public:
    Analyzer(const CompUnit& unit, DiagnosticEngine& diagnostics)
        : unit_(unit), diagnostics_(diagnostics) {}

    SemaResult run() {
        push_scope();
        for (const CompUnit::Item& item : unit_.items) {
            analyze_item(item);
        }
        check_main();
        pop_scope();
        result_.ok = !diagnostics_.has_errors();
        return std::move(result_);
    }

private:
    struct SuppressConstEvalGuard {
        Analyzer& analyzer;

        explicit SuppressConstEvalGuard(Analyzer& analyzer_ref) : analyzer(analyzer_ref) {
            ++analyzer.suppress_const_eval_depth_;
        }
        ~SuppressConstEvalGuard() { --analyzer.suppress_const_eval_depth_; }

        SuppressConstEvalGuard(const SuppressConstEvalGuard&) = delete;
        SuppressConstEvalGuard& operator=(const SuppressConstEvalGuard&) = delete;
        SuppressConstEvalGuard(SuppressConstEvalGuard&&) = delete;
        SuppressConstEvalGuard& operator=(SuppressConstEvalGuard&&) = delete;
    };

    bool const_eval_enabled() const { return suppress_const_eval_depth_ == 0; }

    std::optional<int> lookup_const_value(const Expr& expr) const {
        auto it = result_.const_values.find(&expr);
        if (it == result_.const_values.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void push_scope() { scopes_.emplace_back(); }
    void pop_scope() { scopes_.pop_back(); }

    bool declare(const std::string& name, Symbol symbol, SourceLoc loc) {
        auto& scope = scopes_.back();
        if (scope.find(name) != scope.end()) {
            error(loc, "redefinition of '" + name + "'");
            return false;
        }
        scope.emplace(name, std::move(symbol));
        return true;
    }

    Symbol* resolve(const std::string& name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    Symbol* resolve_current_scope(const std::string& name) {
        auto found = scopes_.back().find(name);
        return found == scopes_.back().end() ? nullptr : &found->second;
    }

    void error(SourceLoc loc, std::string message) {
        diagnostics_.error(DiagnosticStage::Sema, loc, std::move(message));
    }

    void analyze_item(const CompUnit::Item& item) {
        switch (item.kind) {
            case CompUnit::ItemKind::GlobalConst:
                analyze_global_const(item.global_const);
                break;
            case CompUnit::ItemKind::GlobalVar:
                analyze_global_var(item.global_var);
                break;
            case CompUnit::ItemKind::FuncDef:
                analyze_func(item.func_def);
                break;
        }
    }

    void analyze_global_const(const GlobalConstDecl& decl) {
        if (resolve_current_scope(decl.name)) {
            error(decl.loc, "redefinition of '" + decl.name + "'");
            return;
        }
        require_int_expr(*decl.init, "global const initializer");
        auto value = eval_const_expr(*decl.init);
        if (!value) {
            error(decl.loc, "global const '" + decl.name + "' initializer is not a compile-time constant");
            return;
        }
        result_.const_values[&decl] = *value;
        declare(decl.name, Symbol{SymbolKind::Object,
                                  SymbolRef{SymbolStorageKind::GlobalConst, &decl, decl.name},
                                  *value, nullptr},
                decl.loc);
    }

    void analyze_global_var(const GlobalVarDecl& decl) {
        if (resolve_current_scope(decl.name)) {
            error(decl.loc, "redefinition of '" + decl.name + "'");
            return;
        }
        require_int_expr(*decl.init, "global variable initializer");
        auto value = eval_const_expr(*decl.init);
        if (!value) {
            error(decl.loc, "global variable '" + decl.name + "' initializer is not a compile-time constant");
            return;
        }
        result_.const_values[&decl] = *value;
        declare(decl.name, Symbol{SymbolKind::Object,
                                  SymbolRef{SymbolStorageKind::GlobalVar, &decl, decl.name},
                                  std::nullopt, nullptr},
                decl.loc);
    }

    void analyze_func(const FuncDef& func) {
        if (resolve_current_scope(func.name)) {
            error(func.loc, "redefinition of '" + func.name + "'");
            return;
        }

        Symbol func_symbol;
        func_symbol.kind = SymbolKind::Function;
        func_symbol.ref = SymbolRef{SymbolStorageKind::GlobalVar, &func, func.name};
        func_symbol.func = &func;
        declare(func.name, std::move(func_symbol), func.loc);

        current_func_ = &func;
        loop_depth_ = 0;
        push_scope();
        for (const Param& param : func.params) {
            declare(param.name,
                    Symbol{SymbolKind::Object, SymbolRef{SymbolStorageKind::Param, &param, param.name},
                           std::nullopt, nullptr},
                    param.loc);
        }

        StmtFlow flow = analyze_block(*func.body);
        if (func.return_type == FuncReturnType::Int && !flow.returns) {
            error(func.loc, "int function '" + func.name + "' may exit without returning a value");
        }

        pop_scope();
        current_func_ = nullptr;
    }

    StmtFlow analyze_block(const BlockStmt& block) {
        push_scope();
        StmtFlow flow;
        for (const std::unique_ptr<Stmt>& stmt : block.body) {
            if (!stmt) {
                continue;
            }
            StmtFlow stmt_flow = analyze_stmt(*stmt);
            flow.returns = flow.returns || stmt_flow.returns;
            if (flow.returns) {
                break;
            }
        }
        pop_scope();
        return flow;
    }

    StmtFlow analyze_stmt(const Stmt& stmt) {
        switch (stmt.kind) {
            case Stmt::Kind::Block:
                return analyze_block(stmt.block);
            case Stmt::Kind::Empty:
                return {};
            case Stmt::Kind::Expr:
                analyze_expr(*stmt.expr.expr);
                return {};
            case Stmt::Kind::Assign:
                analyze_assign(stmt.assign);
                return {};
            case Stmt::Kind::ConstDecl:
                analyze_local_const(stmt.const_decl);
                return {};
            case Stmt::Kind::VarDecl:
                analyze_local_var(stmt.var_decl);
                return {};
            case Stmt::Kind::If:
                return analyze_if(stmt.if_stmt);
            case Stmt::Kind::While:
                return analyze_while(stmt.while_stmt);
            case Stmt::Kind::Break:
                if (loop_depth_ == 0) {
                    error(stmt.break_stmt.loc, "break statement is not inside a loop");
                }
                return {};
            case Stmt::Kind::Continue:
                if (loop_depth_ == 0) {
                    error(stmt.continue_stmt.loc, "continue statement is not inside a loop");
                }
                return {};
            case Stmt::Kind::Return:
                analyze_return(stmt.return_stmt);
                return {true};
        }
        return {};
    }

    void analyze_assign(const AssignStmt& stmt) {
        Symbol* symbol = resolve(stmt.name);
        if (!symbol) {
            error(stmt.loc, "use of undeclared identifier '" + stmt.name + "'");
        } else if (symbol->kind == SymbolKind::Function) {
            error(stmt.loc, "cannot assign to function '" + stmt.name + "'");
        } else if (symbol->ref.storage == SymbolStorageKind::GlobalConst ||
                   symbol->ref.storage == SymbolStorageKind::LocalConst) {
            error(stmt.loc, "cannot assign to const '" + stmt.name + "'");
        } else {
            result_.assigns[&stmt] = symbol->ref;
        }
        require_int_expr(*stmt.value, "assignment right-hand side");
    }

    void analyze_local_const(const ConstDeclStmt& decl) {
        if (resolve_current_scope(decl.name)) {
            error(decl.loc, "redefinition of '" + decl.name + "'");
            return;
        }
        require_int_expr(*decl.init, "const initializer");
        auto value = eval_const_expr(*decl.init);
        if (!value) {
            error(decl.loc, "local const '" + decl.name + "' initializer is not a compile-time constant");
            return;
        }
        result_.const_values[&decl] = *value;
        declare(decl.name, Symbol{SymbolKind::Object,
                                  SymbolRef{SymbolStorageKind::LocalConst, &decl, decl.name},
                                  *value, nullptr},
                decl.loc);
    }

    void analyze_local_var(const VarDeclStmt& decl) {
        if (resolve_current_scope(decl.name)) {
            error(decl.loc, "redefinition of '" + decl.name + "'");
            return;
        }
        require_int_expr(*decl.init, "variable initializer");
        declare(decl.name, Symbol{SymbolKind::Object,
                                  SymbolRef{SymbolStorageKind::LocalVar, &decl, decl.name},
                                  std::nullopt, nullptr},
                decl.loc);
    }

    StmtFlow analyze_if(const IfStmt& stmt) {
        require_int_expr(*stmt.condition, "if condition");
        StmtFlow then_flow = analyze_stmt(*stmt.then_branch);
        StmtFlow else_flow;
        if (stmt.else_branch) {
            else_flow = analyze_stmt(*stmt.else_branch);
        }
        return {stmt.else_branch && then_flow.returns && else_flow.returns};
    }

    StmtFlow analyze_while(const WhileStmt& stmt) {
        require_int_expr(*stmt.condition, "while condition");
        ++loop_depth_;
        StmtFlow body_flow = analyze_stmt(*stmt.body);
        --loop_depth_;

        auto cond_value = eval_const_expr(*stmt.condition);
        const bool infinite = cond_value && *cond_value != 0;
        return {infinite && body_flow.returns && !contains_current_loop_break(*stmt.body)};
    }

    void analyze_return(const ReturnStmt& stmt) {
        if (!current_func_) {
            error(stmt.loc, "return statement is not inside a function");
            return;
        }
        if (current_func_->return_type == FuncReturnType::Void) {
            if (stmt.value) {
                error(stmt.loc, "void function '" + current_func_->name + "' should not return a value");
                require_int_expr(**stmt.value, "return value");
            }
            return;
        }
        if (!stmt.value) {
            error(stmt.loc, "int function '" + current_func_->name + "' must return a value");
            return;
        }
        require_int_expr(**stmt.value, "return value");
    }

    ExprValueType analyze_expr(const Expr& expr) {
        switch (expr.kind) {
            case Expr::Kind::IntLiteral:
                result_.expr_types[&expr] = ExprValueType::Int;
                result_.const_values[&expr] = expr.int_literal.value;
                return ExprValueType::Int;
            case Expr::Kind::Ident:
                return analyze_ident(expr, expr.ident);
            case Expr::Kind::Binary:
                return analyze_binary(expr, expr.binary);
            case Expr::Kind::Unary:
                return analyze_unary(expr, expr.unary);
            case Expr::Kind::Call:
                return analyze_call(expr, expr.call);
        }
        result_.expr_types[&expr] = ExprValueType::Int;
        return ExprValueType::Int;
    }

    ExprValueType analyze_ident(const Expr& expr, const IdentExpr& ident) {
        Symbol* symbol = resolve(ident.name);
        if (!symbol) {
            error(ident.loc, "use of undeclared identifier '" + ident.name + "'");
            result_.expr_types[&expr] = ExprValueType::Int;
            return ExprValueType::Int;
        }
        if (symbol->kind == SymbolKind::Function) {
            error(ident.loc, "function '" + ident.name + "' cannot be used as a value");
            result_.expr_types[&expr] = ExprValueType::Int;
            return ExprValueType::Int;
        }
        result_.idents[&ident] = symbol->ref;
        if (symbol->const_value) {
            result_.const_values[&ident] = *symbol->const_value;
            result_.const_values[&expr] = *symbol->const_value;
        }
        result_.expr_types[&expr] = ExprValueType::Int;
        return ExprValueType::Int;
    }

    ExprValueType analyze_binary(const Expr& expr, const BinaryExpr& binary) {
        require_int_expr(*binary.lhs, "left operand");

        std::optional<int> short_circuit_value;
        if (binary.op == BinaryOp::And) {
            if (auto lhs_const = lookup_const_value(*binary.lhs)) {
                if (*lhs_const == 0) {
                    short_circuit_value = 0;
                }
            }
        } else if (binary.op == BinaryOp::Or) {
            if (auto lhs_const = lookup_const_value(*binary.lhs)) {
                if (*lhs_const != 0) {
                    short_circuit_value = 1;
                }
            }
        }

        if (short_circuit_value) {
            SuppressConstEvalGuard guard(*this);
            require_int_expr(*binary.rhs, "right operand");
            result_.expr_types[&expr] = ExprValueType::Int;
            result_.const_values[&expr] = *short_circuit_value;
            return ExprValueType::Int;
        }

        require_int_expr(*binary.rhs, "right operand");
        result_.expr_types[&expr] = ExprValueType::Int;
        if (const_eval_enabled()) {
            if (auto value = eval_const_expr(expr)) {
                result_.const_values[&expr] = *value;
            }
        }
        return ExprValueType::Int;
    }

    ExprValueType analyze_unary(const Expr& expr, const UnaryExpr& unary) {
        require_int_expr(*unary.operand, "unary operand");
        result_.expr_types[&expr] = ExprValueType::Int;
        if (const_eval_enabled()) {
            if (auto value = eval_const_expr(expr)) {
                result_.const_values[&expr] = *value;
            }
        }
        return ExprValueType::Int;
    }

    ExprValueType analyze_call(const Expr& expr, const CallExpr& call) {
        Symbol* symbol = resolve(call.callee);
        if (!symbol) {
            error(call.loc, "call to undeclared function '" + call.callee + "'");
            result_.expr_types[&expr] = ExprValueType::Int;
            return ExprValueType::Int;
        }
        if (symbol->kind != SymbolKind::Function || !symbol->func) {
            error(call.loc, "'" + call.callee + "' is not a function");
            result_.expr_types[&expr] = ExprValueType::Int;
            return ExprValueType::Int;
        }
        const FuncDef* func = symbol->func;
        result_.calls[&call] = func;
        if (call.args.size() != func->params.size()) {
            error(call.loc, "function '" + call.callee + "' expects " +
                                std::to_string(func->params.size()) + " argument(s), got " +
                                std::to_string(call.args.size()));
        }
        for (const std::unique_ptr<Expr>& arg : call.args) {
            require_int_expr(*arg, "function argument");
        }
        ExprValueType type = func->return_type == FuncReturnType::Void ? ExprValueType::Void : ExprValueType::Int;
        result_.expr_types[&expr] = type;
        return type;
    }

    bool require_int_expr(const Expr& expr, const char* context) {
        ExprValueType type = analyze_expr(expr);
        if (type == ExprValueType::Void) {
            error(expr_loc(expr), std::string("void expression cannot be used as ") + context);
            return false;
        }
        return true;
    }

    std::optional<int> eval_const_expr(const Expr& expr) {
        if (!const_eval_enabled()) {
            return std::nullopt;
        }
        if (auto cached = lookup_const_value(expr)) {
            return cached;
        }

        switch (expr.kind) {
            case Expr::Kind::IntLiteral: {
                const int value = expr.int_literal.value;
                result_.const_values[&expr] = value;
                return value;
            }
            case Expr::Kind::Ident: {
                Symbol* symbol = resolve(expr.ident.name);
                if (!symbol || symbol->kind != SymbolKind::Object || !symbol->const_value) {
                    return std::nullopt;
                }
                result_.const_values[&expr.ident] = *symbol->const_value;
                result_.const_values[&expr] = *symbol->const_value;
                return *symbol->const_value;
            }
            case Expr::Kind::Unary: {
                auto value = eval_const_expr(*expr.unary.operand);
                if (!value) {
                    return std::nullopt;
                }
                switch (expr.unary.op) {
                    case UnaryOp::Plus:
                        return *value;
                    case UnaryOp::Minus:
                        return -*value;
                    case UnaryOp::Not:
                        return !*value;
                }
                return std::nullopt;
            }
            case Expr::Kind::Binary: {
                switch (expr.binary.op) {
                    case BinaryOp::And: {
                        auto lhs = eval_const_expr(*expr.binary.lhs);
                        if (!lhs) {
                            return std::nullopt;
                        }
                        if (*lhs == 0) {
                            result_.const_values[&expr] = 0;
                            return 0;
                        }
                        auto rhs = eval_const_expr(*expr.binary.rhs);
                        if (!rhs) {
                            return std::nullopt;
                        }
                        const int value = static_cast<int>(*rhs != 0);
                        result_.const_values[&expr] = value;
                        return value;
                    }
                    case BinaryOp::Or: {
                        auto lhs = eval_const_expr(*expr.binary.lhs);
                        if (!lhs) {
                            return std::nullopt;
                        }
                        if (*lhs != 0) {
                            result_.const_values[&expr] = 1;
                            return 1;
                        }
                        auto rhs = eval_const_expr(*expr.binary.rhs);
                        if (!rhs) {
                            return std::nullopt;
                        }
                        const int value = static_cast<int>(*rhs != 0);
                        result_.const_values[&expr] = value;
                        return value;
                    }
                    default:
                        break;
                }

                auto lhs = eval_const_expr(*expr.binary.lhs);
                auto rhs = eval_const_expr(*expr.binary.rhs);
                if (!lhs || !rhs) {
                    return std::nullopt;
                }
                std::optional<int> value;
                switch (expr.binary.op) {
                    case BinaryOp::Add:
                        value = *lhs + *rhs;
                        break;
                    case BinaryOp::Sub:
                        value = *lhs - *rhs;
                        break;
                    case BinaryOp::Mul:
                        value = *lhs * *rhs;
                        break;
                    case BinaryOp::Div:
                        if (*rhs == 0) {
                            if (const_eval_diagnosed_.insert(&expr).second) {
                                error(expr.binary.loc,
                                      "division by zero in compile-time constant expression");
                            }
                            return std::nullopt;
                        }
                        value = *lhs / *rhs;
                        break;
                    case BinaryOp::Mod:
                        if (*rhs == 0) {
                            if (const_eval_diagnosed_.insert(&expr).second) {
                                error(expr.binary.loc,
                                      "modulo by zero in compile-time constant expression");
                            }
                            return std::nullopt;
                        }
                        value = *lhs % *rhs;
                        break;
                    case BinaryOp::Lt:
                        value = *lhs < *rhs;
                        break;
                    case BinaryOp::Le:
                        value = *lhs <= *rhs;
                        break;
                    case BinaryOp::Gt:
                        value = *lhs > *rhs;
                        break;
                    case BinaryOp::Ge:
                        value = *lhs >= *rhs;
                        break;
                    case BinaryOp::Eq:
                        value = *lhs == *rhs;
                        break;
                    case BinaryOp::Ne:
                        value = *lhs != *rhs;
                        break;
                    default:
                        return std::nullopt;
                }
                result_.const_values[&expr] = *value;
                return value;
            }
            case Expr::Kind::Call:
                return std::nullopt;
        }
        return std::nullopt;
    }

    bool contains_current_loop_break(const Stmt& stmt) const {
        switch (stmt.kind) {
            case Stmt::Kind::Break:
                return true;
            case Stmt::Kind::Block:
                for (const std::unique_ptr<Stmt>& child : stmt.block.body) {
                    if (child && contains_current_loop_break(*child)) {
                        return true;
                    }
                }
                return false;
            case Stmt::Kind::If:
                return contains_current_loop_break(*stmt.if_stmt.then_branch) ||
                       (stmt.if_stmt.else_branch && contains_current_loop_break(*stmt.if_stmt.else_branch));
            case Stmt::Kind::While:
                return false;
            default:
                return false;
        }
    }

    SourceLoc expr_loc(const Expr& expr) const {
        switch (expr.kind) {
            case Expr::Kind::IntLiteral:
                return expr.int_literal.loc;
            case Expr::Kind::Ident:
                return expr.ident.loc;
            case Expr::Kind::Binary:
                return expr.binary.loc;
            case Expr::Kind::Unary:
                return expr.unary.loc;
            case Expr::Kind::Call:
                return expr.call.loc;
        }
        return SourceLoc{};
    }

    void check_main() {
        Symbol* symbol = nullptr;
        if (!scopes_.empty()) {
            auto found = scopes_.front().find("main");
            if (found != scopes_.front().end()) {
                symbol = &found->second;
            }
        }
        if (!symbol || symbol->kind != SymbolKind::Function || !symbol->func) {
            error(SourceLoc{0, 0}, "program must define int main()");
            return;
        }
        if (symbol->func->return_type != FuncReturnType::Int || !symbol->func->params.empty()) {
            error(symbol->func->loc, "main must have signature int main()");
        }
    }

    const CompUnit& unit_;
    DiagnosticEngine& diagnostics_;
    SemaResult result_;
    std::vector<std::unordered_map<std::string, Symbol>> scopes_;
    const FuncDef* current_func_ = nullptr;
    int loop_depth_ = 0;
    int suppress_const_eval_depth_ = 0;
    std::unordered_set<const Expr*> const_eval_diagnosed_;
};

}  // namespace

SemaResult analyze(const CompUnit& unit, DiagnosticEngine& diagnostics) {
    Analyzer analyzer(unit, diagnostics);
    return analyzer.run();
}

}  // namespace toyc
