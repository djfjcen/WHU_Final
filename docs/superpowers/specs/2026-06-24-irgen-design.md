# IRGen 设计（路径 1：最小自解析）

> 日期：2026-06-24
> 负责人：IR + 优化组（分工第 3 人）
> 关联：[`2026-06-22-toyc-compiler-design.md`](./2026-06-22-toyc-compiler-design.md) §6.3、[`2026-06-22-toyc-ir-optim-design.md`](./2026-06-22-toyc-ir-optim-design.md) §3/§4.5/§7、[`docs/IR-Sema接口请求反馈.md`](../../IR-Sema接口请求反馈.md)
> 状态：**已确认，待实现**

## 0. 背景与定位

IR 数据结构（`Value`/`User` + 指令层次 + `Module`/`Function`/`BasicBlock`）、`IRPrinter`、`IRBuilder` 已完成（`9a46356` 及之前提交），但缺 **IRGen**——AST → IR 的翻译遍历，IR 投资"悬空"。

Sema 组（分工第 2 人）尚未启动。本设计采用 [`IR-Sema接口请求反馈.md`](../../IR-Sema接口请求反馈.md) §3 **方案 3（IRGen 自解析）** 作为 fallback：IRGen 自建符号栈 + 常量折叠器打通管线，待真 Sema 交付 `SemaResult` 后再切换。

**目标**：让 `-dump-ir` 从源码跑通，IR 数据结构产生实际价值，并为 codegen 解锁上游。

## 1. 范围与边界

- **输入**：`CompUnit`（已过 `validate_comp_unit`，结构完整：函数体非空、声明带初始化等）
- **输出**：`std::unique_ptr<Module>`，线性**非 SSA** IR（变量经 `alloca/load/store`，SSA 化交给后续 mem2reg）
- **自解析职责（最小，仅 IRGen 运行必需）**：
  - 符号解析：`name` → 存储类 + 槽/值
  - 常量折叠：const 初值 + 全局初值编译期求值（Q5 已拍板：全局初值不允许运行期表达式）
- **不做**（全部留给未来 Sema）：语义检查（未声明使用、重定义、void 误用、break/continue 上下文、int 函数路径 return 等）
- **错误行为**：假定输入合法；遇理论不可达态 → `DiagnosticEngine::error(DiagnosticStage::Sema, ...)` + fail-fast（返回 nullptr）
- **产出消费**：先 `IRPrinter`（`-dump-ir`）；codegen 接入后给 codegen

## 2. 文件与接入

| 文件 | 改动 |
|---|---|
| `include/toyc/irgen.h` | **新增**：`IRGenerator` 类（继承 `ASTVisitor`）+ 入口 `generate(const CompUnit&, DiagnosticEngine&) -> std::unique_ptr<Module>` |
| `src/ir/irgen.cpp` | **新增**：符号栈、折叠器、各 `visit_*` 翻译实现 |
| `include/toyc/options.h` | 加 `bool dump_ir = false;` |
| `src/driver/options.cpp` | 解析 `-dump-ir` / `-dump-asm`（都置 `dump_ir`） |
| `src/driver/main.cpp` | `run_frontend` 在 AST 后跑 IRGen；`dump_ir` → `print_module(std::cerr)` |
| `CMakeLists.txt` | 加 `src/ir/irgen.cpp` 到 `toyc-compiler` 编译目标 |

**符号栈 + 折叠器不建 `src/sema/`**（那是 Sema 组领地；路径 1 临时逻辑作为 `IRGenerator` 内部实现细节，集中在 `irgen.cpp`）。切换真 Sema 时整体拆走，不牵连外部。

## 3. 代码组织决策（方案 A）

`IRGenerator` 单类继承 `ASTVisitor`，符号栈 / 常量折叠器 / 循环栈都是它的**内部成员**。不分多文件（YAGNI——路径 1 是临时代码，过度分层增加切换成本）。

## 4. IRGenerator 内部状态

- **符号栈** `std::vector<Scope>`，`Scope = std::unordered_map<std::string, Symbol>`
  - `struct Symbol { SymbolStorageKind kind; Value* addr = nullptr; std::optional<int> const_value; }`
  - 局部 var / 形参 → `addr` = alloca 槽
  - 局部 const / 全局 const → `const_value`
  - 全局 var → `addr` = 全局地址（`GlobalVar::addr`）
- **全局表**：
  - 函数签名：复用 `ast_access.h::build_func_signature_map()`（name → `FuncReturnType`）
  - 全局变量/常量：遍历 `Module::globals()`（含 `is_const` / `init` 值）
- **循环栈** `std::vector<LoopFrame>`，`struct LoopFrame { BasicBlock* cond; BasicBlock* exit; }`：`break` → exit，`continue` → cond
- **IRBuilder**：持当前 insert point（`set_insert_point`）
- 当前函数 / `Module*` 引用

> `SymbolStorageKind` 复用 `ast_contract.h` 已定义的五类（`GlobalConst/GlobalVar/LocalConst/LocalVar/Param`）。

## 5. 翻译规则

| AST 节点 | IRGen 行为 |
|---|---|
| `CompUnit` | 先扫全局声明（登记 `Module::create_global` + 折叠初值）与函数签名；再逐函数生成 |
| `FuncDef` | `create_function(name, ret, param_count)`；建 `entry` 块；形参 → entry `alloca` + `store %arg.N` |
| `VarDeclStmt` | entry 块 `alloca` + 求值 init + `store` |
| `ConstDeclStmt` | 折叠 init → `const_value`（不 alloca） |
| `AssignStmt` | 求值 rhs → `store` 到局部槽 / 全局 addr |
| `IdentExpr`（读） | const → 立即数（`get_constant`）；var/param → `load` 槽；全局 var → `load` addr |
| `BinaryExpr` | `Or/And` → 短路（基本块 + `cond_br`，结果 0/1）；`Add/Sub/Mul/Div/Mod` → `BinaryInst`；`Lt/Le/Gt/Ge/Eq/Ne` → `ICmpInst` |
| `UnaryExpr` | `Minus` → `NegInst`；`Not` → `icmp eq 0`；`Plus` → 透传 |
| `CallExpr` | 求值 args → `CallInst`（`returns_void` 取自签名表）；void call 在语句位不求值结果 |
| `IfStmt` | 求值 cond → `cond_br(then, else/merge)`；分支末 `br` 到 merge |
| `WhileStmt` | 建 cond 块 / body 块 / exit 块；body 末 `br` 回 cond；`break/continue` 走循环栈 |
| `BreakStmt` / `ContinueStmt` | `br` 到循环栈顶的 exit / cond |
| `ReturnStmt` | 求值 value（若有）→ `RetInst`；void 函数无值 |

**形参处理**：每个形参在 `entry` 块 `alloca` 一个槽并 `store %arg.N`，统一内存模型（形参可被赋值，且便于后续 mem2reg 一并提升）。

**短路求值**（`&&` / `||`）：用基本块 + `cond_br` 显式建模，结果为 0/1（i32），符合设计 §6.3。

## 6. 常量折叠器

`std::optional<int> try_fold(const Expr& expr) const`

支持：
- `IntLiteral` → 字面量
- `Ident` → 若解析为 const，返回 `const_value`
- `Binary`（算逻）→ 两操作数均可折叠时折叠（注意除零：作为编译期非法，诊断 + fail-fast）
- `Unary` → `Minus` 取反、`Not` 逻辑非（!0=1, !x=0）、`Plus` 透传

用途：const 初值求值、全局初值求值（`create_global` 需 `int`）、const 引用内联为立即数。

## 7. 错误处理

fail-fast 原则：假定输入已合法。理论不可达态（如符号未找到、除零常量折叠）→ `DiagnosticEngine::error(DiagnosticStage::Sema, loc, msg)` + 返回失败，`main` 退出非 0。不做防御性兜底。

## 8. 测试

**端到端 golden file**（与前端 parser 回归同套路）：
- `test/regression/irgen/` 下 `.tc` + `.expected`（IR 文本），CTest 对照
- 复用 `test/*.tc`（`fib.tc` / `sample.tc` 等）补 `.expected`
- 覆盖：常量返回、算术、if/while、短路求值、函数调用、全局 var/const、递归

codegen 出来后追加 oracle 退出码对照（`.s` 运行 ↔ gcc 同源运行）。

## 9. 接入 main 的默认行为

- `-dump-ir`（或 `-dump-asm`）→ IRGen 产 `Module` → `print_module(std::cerr)`，**输出到 stderr**（与 `-dump-ast` 一致，stdout 留给最终汇编）
- 无 dump 标志时：维持现状（报 "codegen not implemented"）—— IRGen 暂不默认触发。等 codegen（IR→asm）落地后，默认模式才走 `IRGen → Codegen → stdout`

## 10. 决策记录

| 决策 | 选择 | 理由 |
|---|---|---|
| 路径 | 路径 1（自解析）= 接口文档方案 3 fallback | Sema 组未启动；先打通，未来切换 |
| Sema 边界 | 最小自解析（不做语义检查） | 路径 1 精神是先打通；语义检查是 Sema 的活，IRGen 重复收益低、后期要删 |
| 代码组织 | 方案 A（单类 + 内部成员） | 临时代码不过度抽象；切换真 Sema 时整体拆走 |
| 形参 | alloca + store %arg.N | 统一内存模型，便于 mem2reg |
| 测试 | 端到端 golden file | 与前端回归同套路，codegen 未就绪时最有效 |
