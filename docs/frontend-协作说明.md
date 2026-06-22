# 前端模块协作文档

> 对应设计文档 [`docs/superpowers/specs/2026-06-22-toyc-compiler-design.md`](../superpowers/specs/2026-06-22-toyc-compiler-design.md) §4、§9。  
> 词汇与 Token 对照见 [`词汇翻译表.md`](../../词汇翻译表.md)。

## 1. 模块职责

| 模块 | 文件 | 输入 | 输出 | 负责同学 |
|------|------|------|------|----------|
| Lexer | `include/toyc/lexer.h` `src/frontend/lexer.cpp` | 源码字符流 | `Token` 流 | 前端 |
| Parser | `include/toyc/parser.h` `src/frontend/parser.cpp` | `Token` 流 | `CompUnit` AST | 前端 |
| AST | `include/toyc/ast.h` `src/frontend/ast.cpp` | — | 数据结构 + 结构校验 | 前端 |
| ASTVisitor | `include/toyc/ast_visitor.h` `src/frontend/ast_visitor.cpp` | `CompUnit` | 遍历回调 | 前端 |
| ASTPrinter | `include/toyc/ast_printer.h` `src/frontend/ast_printer.cpp` | `CompUnit` | 可读树形文本 | 前端 |
| Diagnostics | `include/toyc/diagnostics.h` `src/driver/diagnostics.cpp` | 各阶段错误 | 统一诊断列表 | Driver（前端共建） |
| Options | `include/toyc/options.h` `src/driver/options.cpp` | argv | `CompilerOptions` | Driver |
| Sema（待实现） | `src/sema/`（规划） | `CompUnit` | 符号表 / 检查结果 | 语义组 |

**边界约定**：Parser 只做语法结构，不做符号表、不做类型检查、不做常量折叠。Sema 组通过 `ASTVisitor` 遍历 AST，不修改 Parser 接口。

## 2. 数据流

```
stdin
  └─► Lexer(input, DiagnosticEngine)
        └─► Parser(lexer, DiagnosticEngine)
              └─► std::unique_ptr<CompUnit>
                    ├─► validate_comp_unit()   AST 结构不变量
                    ├─► -dump-ast  → ASTPrinter → stderr
                    └─► walk_comp_unit(visitor) → Sema
```

## 3. AST 节点一览

### 3.1 编译单元

- `CompUnit`：顶层项列表 `items`
  - `GlobalConstDecl`：`const int name = init`
  - `GlobalVarDecl`：`int name = init`
  - `FuncDef`：函数签名 + `BlockStmt` 函数体

### 3.2 语句 `Stmt::Kind`

| Kind | 含义 | 主要字段 |
|------|------|----------|
| `Block` | `{ ... }` | `body: vector<Stmt>` |
| `Empty` | `;` | — |
| `Expr` | 表达式语句 | `expr` |
| `Assign` | `id = expr;` | `name`, `value` |
| `ConstDecl` | 局部常量声明 | `name`, `init` |
| `VarDecl` | 局部变量声明 | `name`, `init` |
| `If` | 条件分支 | `condition`, `then_branch`, `else_branch?` |
| `While` | 循环 | `condition`, `body` |
| `Break` / `Continue` | 循环控制 | — |
| `Return` | 返回 | `value?` |

### 3.3 表达式 `Expr::Kind`

| Kind | 含义 | 主要字段 |
|------|------|----------|
| `IntLiteral` | 整数字面量 | `value`, `lexeme` |
| `Ident` | 标识符引用 | `name` |
| `Binary` | 二元运算 | `op`, `lhs`, `rhs` |
| `Unary` | 一元运算 | `op`, `operand` |
| `Call` | 函数调用 | `callee`, `args` |

所有节点均带 `SourceLoc { line, column }`，供诊断与 Sema 定位。

## 4. AST 遍历（Sema 接入）

继承 `ASTVisitor` 并重写关心的节点方法：

```cpp
#include "toyc/ast_visitor.h"

class MySema : public toyc::ASTVisitor {
public:
    void visit_func_def(const toyc::FuncDef& func) override {
        // 登记函数、进入作用域 ...
        toyc::ASTVisitor::visit_func_def(func);  // 继续遍历函数体
    }
};

toyc::walk_comp_unit(*unit, sema);
```

也可直接调用 `walk_stmt` / `walk_expr` / `walk_block`。

## 5. 统一诊断格式

```
error [lex] 3:5: unterminated block comment
error [parse] 7:12: expected ';' after variable declaration
error [ast] 2:1: function 'foo' is missing a body
```

API：`DiagnosticEngine::error(stage, loc, message)`，`emit_all(std::cerr)` 输出全部诊断。

## 6. Parser 实现要点

- **算法**：递归下降，左递归产生式用 `while` 循环展开。
- **顶层消歧**：`int ID` 后看 `=` → 全局变量，看 `(` → 函数定义。
- **语句消歧**：`ID` 后看 `=` → 赋值，否则按表达式语句解析。
- **错误策略**：fail fast，首个错误写入 `DiagnosticEngine` 后停止。

## 7. 调试与测试

```powershell
cmake -S . -B build
cmake --build build --config Debug

# 词法
build\Debug\toyc-compiler.exe -dump-tokens < test\sample.tc

# 语法 + AST
build\Debug\toyc-compiler.exe -dump-ast < test\sample.tc

# Parser 回归（14 个用例）
ctest --test-dir build -C Debug -R parser_regression
```

详见 [`test/regression/parser/README.md`](../test/regression/parser/README.md)。

## 8. 与 Sema 组的接口约定

1. `#include "toyc/ast.h"`、`"toyc/ast_visitor.h"`、`"toyc/diagnostics.h"`
2. 从 `Parser::parse_comp_unit()` 取得 AST；失败时读 `DiagnosticEngine`
3. 调用 `validate_comp_unit()` 后可假定 AST 结构完整（函数体非空、初始化表达式存在等）
4. 语义错误用 `DiagnosticStage::Sema` 报告，格式与前端一致

## 9. 演进状态（M1）

- [x] 源码迁到 `src/frontend/` + `src/driver/`（公共头文件保留在 `include/toyc/`）
- [x] 统一 `DiagnosticEngine` 与 `CompilerOptions`
- [x] `ASTVisitor` + `walk_*` 遍历 API
- [x] `test/regression/parser/` + CTest
- [ ] Sema：`symbol_table` / `checker` / `const_eval`（语义组）

## 10. 参考文法

权威文法见 [`任务要求.md`](../../任务要求.md)；Token 对照见 [`词汇翻译表.md`](../../词汇翻译表.md)。
