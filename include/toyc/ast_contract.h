#pragma once

#include "toyc/ast.h"

namespace toyc {

// 供 Sema / IRGen 共用的符号存储类别（方案 2 side-table 中 SymbolRef 建议使用）。
enum class SymbolStorageKind {
    GlobalConst,
    GlobalVar,
    LocalConst,
    LocalVar,
    Param,
};

enum class ValueType { Int, Void };

const char* symbol_storage_kind_name(SymbolStorageKind kind);
const char* value_type_name(ValueType kind);

// Sema / IRGen 可依赖的 AST 生命周期约定（见 docs/IR-Sema接口请求反馈-前端组.md）。
// - CompUnit 由 unique_ptr 持有，check/analyze/codegen 期间不移动、不重建 AST 子树。
// - side-table 以 const IdentExpr* / const AssignStmt* 等为 key 安全。
// - Parser 不写入任何语义标注字段；语义结果由 SemaResult side-table 交付。
// - 全局变量初值不允许运行期表达式（Q5 已拍板）；Sema 检查编译期可求值。

}  // namespace toyc
