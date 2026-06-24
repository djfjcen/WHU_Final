#include "toyc/irgen.h"

#include "toyc/diagnostics.h"
#include "toyc/ir_builder.h"

#include <memory>

namespace toyc {

namespace {

FuncRet to_func_ret(FuncReturnType t) {
    return t == FuncReturnType::Int ? FuncRet::Int : FuncRet::Void;
}

}  // namespace

Value* IRGenerator::alloca_in_entry() {
    auto inst = std::make_unique<AllocaInst>(module_->fresh_id());
    Value* raw = inst.get();
    alloca_pt_ = entry_->insts().insert(alloca_pt_, std::move(inst));
    ++alloca_pt_;
    return raw;
}

void IRGenerator::push_scope() { scopes_.emplace_back(); }
void IRGenerator::pop_scope() { scopes_.pop_back(); }

void IRGenerator::declare(const std::string& name, Symbol sym) {
    scopes_.back().emplace(name, std::move(sym));
}

Symbol* IRGenerator::resolve(const std::string& name) {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto found = it->find(name);
        if (found != it->end()) {
            return &found->second;
        }
    }
    return nullptr;
}

std::optional<int> IRGenerator::try_fold(const Expr& expr) const {
    if (const IntLiteralExpr* lit = as_int_literal(expr)) {
        return lit->value;
    }
    if (const IdentExpr* id = as_ident(expr)) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(id->name);
            if (found != it->end() && found->second.const_value) {
                return found->second.const_value;
            }
        }
        return std::nullopt;
    }
    if (const BinaryExpr* b = as_binary(expr)) {
        auto l = try_fold(*b->lhs);
        auto r = try_fold(*b->rhs);
        if (!l || !r) return std::nullopt;
        switch (b->op) {
            case BinaryOp::Add: return *l + *r;
            case BinaryOp::Sub: return *l - *r;
            case BinaryOp::Mul: return *l * *r;
            case BinaryOp::Div: return *r == 0 ? std::optional<int>{} : *l / *r;
            case BinaryOp::Mod: return *r == 0 ? std::optional<int>{} : *l % *r;
            case BinaryOp::Lt: return *l < *r;
            case BinaryOp::Le: return *l <= *r;
            case BinaryOp::Gt: return *l > *r;
            case BinaryOp::Ge: return *l >= *r;
            case BinaryOp::Eq: return *l == *r;
            case BinaryOp::Ne: return *l != *r;
            case BinaryOp::And: return *l && *r;
            case BinaryOp::Or: return *l || *r;
        }
        return std::nullopt;
    }
    if (const UnaryExpr* u = as_unary(expr)) {
        auto v = try_fold(*u->operand);
        if (!v) return std::nullopt;
        switch (u->op) {
            case UnaryOp::Plus: return *v;
            case UnaryOp::Minus: return -*v;
            case UnaryOp::Not: return !*v;
        }
    }
    return std::nullopt;
}

Value* IRGenerator::eval_expr(const Expr& expr) {
    last_value_ = nullptr;
    walk_expr(expr, *this);
    return last_value_;
}

void IRGenerator::visit_global_const(const GlobalConstDecl& decl) {
    auto folded = try_fold(*decl.init);
    if (!folded) { had_error_ = true; return; }
    module_->create_global(decl.name, *folded, /*is_const=*/true);
    declare(decl.name, Symbol{nullptr, *folded});
}

void IRGenerator::visit_global_var(const GlobalVarDecl& decl) {
    auto folded = try_fold(*decl.init);
    if (!folded) { had_error_ = true; return; }
    GlobalVar* gv = module_->create_global(decl.name, *folded, /*is_const=*/false);
    declare(decl.name, Symbol{gv->addr, std::nullopt});
}

void IRGenerator::visit_comp_unit(const CompUnit& unit) {
    func_sigs_ = build_func_signature_map(unit);
    push_scope();  // global scope
    for (const CompUnit::Item& item : unit.items) {
        if (item.kind == CompUnit::ItemKind::GlobalConst) {
            visit_global_const(item.global_const);
        } else if (item.kind == CompUnit::ItemKind::GlobalVar) {
            visit_global_var(item.global_var);
        } else {
            visit_func_def(item.func_def);
        }
    }
    pop_scope();
}

void IRGenerator::visit_func_def(const FuncDef& func) {
    Function* fn = module_->create_function(func.name, to_func_ret(func.return_type),
                                            static_cast<unsigned>(func.params.size()));
    entry_ = fn->create_block();
    alloca_pt_ = entry_->insts().begin();
    builder_ = std::make_unique<IRBuilder>(*module_, entry_);
    current_ret_ = func.return_type;
    push_scope();  // function scope (params + top-level locals), sits on the global scope

    for (unsigned i = 0; i < func.params.size(); ++i) {
        Value* slot = alloca_in_entry();
        builder_->create_store(slot, fn->param(i));
        declare(func.params[i].name, Symbol{slot, std::nullopt});
    }

    walk_block(*func.body, *this);

    pop_scope();
    entry_ = nullptr;
    builder_.reset();
}

void IRGenerator::visit_block_stmt(const BlockStmt& node) {
    push_scope();
    walk_block(node, *this);
    pop_scope();
}
void IRGenerator::visit_empty_stmt(const EmptyStmt&) {}
void IRGenerator::visit_expr_stmt(const ExprStmt& node) { eval_expr(*node.expr); }

void IRGenerator::visit_assign_stmt(const AssignStmt& node) {
    Symbol* sym = resolve(node.name);
    if (!sym) { had_error_ = true; return; }
    Value* value = eval_expr(*node.value);
    builder_->create_store(sym->addr, value);
}

void IRGenerator::visit_const_decl_stmt(const ConstDeclStmt& node) {
    auto folded = try_fold(*node.init);
    if (!folded) { had_error_ = true; return; }
    declare(node.name, Symbol{nullptr, *folded});
}

void IRGenerator::visit_var_decl_stmt(const VarDeclStmt& node) {
    Value* slot = alloca_in_entry();
    declare(node.name, Symbol{slot, std::nullopt});
    Value* init = eval_expr(*node.init);
    builder_->create_store(slot, init);
}

void IRGenerator::visit_if_stmt(const IfStmt& node) {
    Value* cond = eval_expr(*node.condition);
    BasicBlock* then_bb = entry_->parent()->create_block();
    BasicBlock* else_bb = node.else_branch ? entry_->parent()->create_block() : nullptr;
    BasicBlock* merge_bb = entry_->parent()->create_block();
    if (else_bb) {
        builder_->create_cond_br(cond, then_bb, else_bb);
    } else {
        builder_->create_cond_br(cond, then_bb, merge_bb);
    }

    builder_->set_insert_point(then_bb);
    walk_stmt(*node.then_branch, *this);
    if (!then_bb->is_terminated()) {
        builder_->create_br(merge_bb);
    }

    if (else_bb) {
        builder_->set_insert_point(else_bb);
        walk_stmt(*node.else_branch, *this);
        if (!else_bb->is_terminated()) {
            builder_->create_br(merge_bb);
        }
    }

    builder_->set_insert_point(merge_bb);
}

void IRGenerator::visit_while_stmt(const WhileStmt& node) {
    BasicBlock* cond_bb = entry_->parent()->create_block();
    BasicBlock* body_bb = entry_->parent()->create_block();
    BasicBlock* exit_bb = entry_->parent()->create_block();
    builder_->create_br(cond_bb);

    builder_->set_insert_point(cond_bb);
    Value* cond = eval_expr(*node.condition);
    builder_->create_cond_br(cond, body_bb, exit_bb);

    loops_.push_back({cond_bb, exit_bb});
    builder_->set_insert_point(body_bb);
    walk_stmt(*node.body, *this);
    if (!body_bb->is_terminated()) {
        builder_->create_br(cond_bb);
    }
    loops_.pop_back();

    builder_->set_insert_point(exit_bb);
}

void IRGenerator::visit_break_stmt(const BreakStmt&) {
    builder_->create_br(loops_.back().exit);
}
void IRGenerator::visit_continue_stmt(const ContinueStmt&) {
    builder_->create_br(loops_.back().cond);
}

void IRGenerator::visit_return_stmt(const ReturnStmt& node) {
    Value* v = nullptr;
    if (node.value.has_value()) {
        v = eval_expr(**node.value);
    }
    builder_->create_ret(v);
}

void IRGenerator::visit_int_literal(const IntLiteralExpr& node) {
    last_value_ = module_->get_constant(node.value);
}

void IRGenerator::visit_ident(const IdentExpr& node) {
    Symbol* sym = resolve(node.name);
    if (!sym) { had_error_ = true; return; }
    if (sym->const_value) {
        last_value_ = module_->get_constant(*sym->const_value);
    } else {
        last_value_ = builder_->create_load(sym->addr);
    }
}

void IRGenerator::visit_binary(const BinaryExpr& node) {
    if (node.op == BinaryOp::And) {
        last_value_ = short_circuit(/*is_and=*/true, *node.lhs, *node.rhs);
        return;
    }
    if (node.op == BinaryOp::Or) {
        last_value_ = short_circuit(/*is_and=*/false, *node.lhs, *node.rhs);
        return;
    }
    Value* lhs = eval_expr(*node.lhs);
    Value* rhs = eval_expr(*node.rhs);
    switch (node.op) {
        case BinaryOp::Add: last_value_ = builder_->create_binary(Opcode::Add, lhs, rhs); break;
        case BinaryOp::Sub: last_value_ = builder_->create_binary(Opcode::Sub, lhs, rhs); break;
        case BinaryOp::Mul: last_value_ = builder_->create_binary(Opcode::Mul, lhs, rhs); break;
        case BinaryOp::Div: last_value_ = builder_->create_binary(Opcode::Sdiv, lhs, rhs); break;
        case BinaryOp::Mod: last_value_ = builder_->create_binary(Opcode::Srem, lhs, rhs); break;
        case BinaryOp::Lt: last_value_ = builder_->create_icmp(Opcode::ICmpSlt, lhs, rhs); break;
        case BinaryOp::Le: last_value_ = builder_->create_icmp(Opcode::ICmpSle, lhs, rhs); break;
        case BinaryOp::Gt: last_value_ = builder_->create_icmp(Opcode::ICmpSgt, lhs, rhs); break;
        case BinaryOp::Ge: last_value_ = builder_->create_icmp(Opcode::ICmpSge, lhs, rhs); break;
        case BinaryOp::Eq: last_value_ = builder_->create_icmp(Opcode::ICmpEq, lhs, rhs); break;
        case BinaryOp::Ne: last_value_ = builder_->create_icmp(Opcode::ICmpNe, lhs, rhs); break;
        case BinaryOp::And: case BinaryOp::Or: break;  // handled above
    }
}

void IRGenerator::visit_unary(const UnaryExpr& node) {
    Value* operand = eval_expr(*node.operand);
    switch (node.op) {
        case UnaryOp::Plus: last_value_ = operand; break;
        case UnaryOp::Minus: last_value_ = builder_->create_neg(operand); break;
        case UnaryOp::Not: {
            Value* zero = module_->get_constant(0);
            last_value_ = builder_->create_icmp(Opcode::ICmpEq, operand, zero);
            break;
        }
    }
}

void IRGenerator::visit_call(const CallExpr& node) {
    std::vector<Value*> args;
    for (const std::unique_ptr<Expr>& arg : node.args) {
        args.push_back(eval_expr(*arg));
    }
    auto it = func_sigs_.find(node.callee);
    bool returns_void = (it != func_sigs_.end()) && (it->second == FuncReturnType::Void);
    last_value_ = builder_->create_call(node.callee, std::move(args), returns_void);
}

Value* IRGenerator::short_circuit(bool is_and, const Expr& lhs_expr, const Expr& rhs_expr) {
    Value* slot = alloca_in_entry();
    Value* zero = module_->get_constant(0);
    Value* one = module_->get_constant(1);

    Value* lhs = eval_expr(lhs_expr);
    Value* lhs_bool = builder_->create_icmp(Opcode::ICmpNe, lhs, zero);

    BasicBlock* rhs_bb = entry_->parent()->create_block();
    BasicBlock* short_bb = entry_->parent()->create_block();
    BasicBlock* merge_bb = entry_->parent()->create_block();
    builder_->create_cond_br(lhs_bool, is_and ? rhs_bb : short_bb,
                                      is_and ? short_bb : rhs_bb);

    builder_->set_insert_point(short_bb);
    builder_->create_store(slot, is_and ? zero : one);
    builder_->create_br(merge_bb);

    builder_->set_insert_point(rhs_bb);
    Value* rhs = eval_expr(rhs_expr);
    Value* rhs_bool = builder_->create_icmp(Opcode::ICmpNe, rhs, zero);
    builder_->create_store(slot, rhs_bool);
    builder_->create_br(merge_bb);

    builder_->set_insert_point(merge_bb);
    return builder_->create_load(slot);
}

std::unique_ptr<Module> IRGenerator::generate(const CompUnit& unit, DiagnosticEngine& diag) {
    diag_ = &diag;
    module_ = std::make_unique<Module>();
    visit_comp_unit(unit);
    if (had_error_) {
        diag.error(DiagnosticStage::Sema, SourceLoc{0, 0}, "IRGen failed (unfoldable constant initializer)");
        return nullptr;
    }
    return std::move(module_);
}

std::unique_ptr<Module> generate(const CompUnit& unit, DiagnosticEngine& diag) {
    IRGenerator g;
    return g.generate(unit, diag);
}

}  // namespace toyc
