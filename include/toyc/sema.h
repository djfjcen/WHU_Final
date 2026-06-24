#pragma once

#include "toyc/ast.h"
#include "toyc/ast_contract.h"
#include "toyc/diagnostics.h"

#include <string>
#include <unordered_map>

namespace toyc {

struct SymbolRef {
    SymbolStorageKind storage = SymbolStorageKind::LocalVar;
    const void* decl = nullptr;
    std::string name;
};

enum class ExprValueType { Int, Void };

struct SemaResult {
    std::unordered_map<const IdentExpr*, SymbolRef> idents;
    std::unordered_map<const AssignStmt*, SymbolRef> assigns;
    std::unordered_map<const CallExpr*, const FuncDef*> calls;
    std::unordered_map<const Expr*, ExprValueType> expr_types;
    std::unordered_map<const void*, int> const_values;
    bool ok = false;
};

SemaResult analyze(const CompUnit& unit, DiagnosticEngine& diagnostics);

}  // namespace toyc
