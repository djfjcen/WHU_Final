#include "toyc/ast_access.h"
#include "toyc/ast_contract.h"

namespace toyc {

const char* symbol_storage_kind_name(SymbolStorageKind kind) {
    switch (kind) {
        case SymbolStorageKind::GlobalConst:
            return "GlobalConst";
        case SymbolStorageKind::GlobalVar:
            return "GlobalVar";
        case SymbolStorageKind::LocalConst:
            return "LocalConst";
        case SymbolStorageKind::LocalVar:
            return "LocalVar";
        case SymbolStorageKind::Param:
            return "Param";
    }
    return "Unknown";
}

const char* value_type_name(ValueType kind) {
    switch (kind) {
        case ValueType::Int:
            return "int";
        case ValueType::Void:
            return "void";
    }
    return "unknown";
}

const IntLiteralExpr* as_int_literal(const Expr& expr) {
    return expr.kind == Expr::Kind::IntLiteral ? &expr.int_literal : nullptr;
}

const IdentExpr* as_ident(const Expr& expr) {
    return expr.kind == Expr::Kind::Ident ? &expr.ident : nullptr;
}

const BinaryExpr* as_binary(const Expr& expr) {
    return expr.kind == Expr::Kind::Binary ? &expr.binary : nullptr;
}

const UnaryExpr* as_unary(const Expr& expr) {
    return expr.kind == Expr::Kind::Unary ? &expr.unary : nullptr;
}

const CallExpr* as_call(const Expr& expr) {
    return expr.kind == Expr::Kind::Call ? &expr.call : nullptr;
}

const BlockStmt* as_block(const Stmt& stmt) {
    return stmt.kind == Stmt::Kind::Block ? &stmt.block : nullptr;
}

const AssignStmt* as_assign(const Stmt& stmt) {
    return stmt.kind == Stmt::Kind::Assign ? &stmt.assign : nullptr;
}

const ConstDeclStmt* as_const_decl(const Stmt& stmt) {
    return stmt.kind == Stmt::Kind::ConstDecl ? &stmt.const_decl : nullptr;
}

const VarDeclStmt* as_var_decl(const Stmt& stmt) {
    return stmt.kind == Stmt::Kind::VarDecl ? &stmt.var_decl : nullptr;
}

FuncSignatureMap build_func_signature_map(const CompUnit& unit) {
    FuncSignatureMap signatures;
    for (const CompUnit::Item& item : unit.items) {
        if (item.kind != CompUnit::ItemKind::FuncDef) {
            continue;
        }
        signatures.emplace(item.func_def.name, item.func_def.return_type);
    }
    return signatures;
}

std::vector<const FuncDef*> collect_func_defs(const CompUnit& unit) {
    std::vector<const FuncDef*> defs;
    for (const CompUnit::Item& item : unit.items) {
        if (item.kind == CompUnit::ItemKind::FuncDef) {
            defs.push_back(&item.func_def);
        }
    }
    return defs;
}

}  // namespace toyc
