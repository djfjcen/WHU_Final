# IRGen 对 Sema 的输入接口需求（请求反馈）

> 发起方：IR + 优化组（分工第 3 人）
> 请求方：**语义分析组**（Sema）；抄送前端组
> 关联：[`docs/superpowers/specs/2026-06-22-toyc-ir-optim-design.md`](./superpowers/specs/2026-06-22-toyc-ir-optim-design.md) §3（IRGen 接口契约）、[`docs/frontend-协作说明.md`](./frontend-协作说明.md)
> 状态：**等待 Sema 反馈**（前端组已于 2026-06-22 回复，见 [`IR-Sema接口请求反馈-前端组.md`](./IR-Sema接口请求反馈-前端组.md)）

---

## 0. 背景与目的

IRGen 的输入是 **Sema 处理后的 AST**。前端 Parser 已落地（lexer/parser/ast/visitor/printer，14 个回归用例），AST 结构与遍历机制已定型。但 IRGen 还需要三类**语义信息**才能正确生成 IR，而这三类按 `frontend-协作说明.md` §19 的边界约定，**属于 Sema 职责**：

1. 标识符引用解析到声明（def-use）
2. 常量折叠结果
3. 表达式/调用的类型信息

本文档：(a) 给出当前 AST 的现状对照；(b) 列出 IRGen 的具体需求；(c) 提出 **3 种"Sema → IRGen 信息传递机制"方案**并给出推荐；(d) 列出请求 Sema 拍板的问题清单。

**目标：在 Sema 动手实现前，把下游（IRGen）的输入接口钉死，避免返工。**

---

## 1. 当前 AST 现状（已具备，前端交付）

来自 `include/toyc/ast.h`，关键节点字段：

| 节点 | 字段 | 备注 |
|------|------|------|
| `IdentExpr` | `name: string`, `loc` | **仅名字，无声明指针** |
| `AssignStmt` | `name: string`, `value: Expr`, `loc` | **赋值目标仅名字**（ToyC 文法 `ID = Expr`） |
| `CallExpr` | `callee: string`, `args: vector<Expr>`, `loc` | **仅函数名，无 FuncDef 指针、无返回类型** |
| `ConstDeclStmt` / `GlobalConstDecl` | `name`, `init: Expr`, `loc` | **init 未折叠** |
| `VarDeclStmt` / `GlobalVarDecl` | `name`, `init: Expr`, `loc` | |
| `Param` | `name`, `loc` | 形参 |
| `FuncDef` | `return_type: FuncReturnType`, `name`, `params`, `body: BlockStmt` | 返回类型在签名上 |
| `Expr` | tagged union（`IntLiteral/Ident/Binary/Unary/Call`），**无 type 字段** | |

**已满足 IRGen 需求的部分**：

- ✅ **遍历机制**：`ASTVisitor` + `walk_comp_unit/stmt/expr/block`（IRGen 继承 visitor 即可接入）。
- ✅ **内存模型**：`unique_ptr` 链，AST 存活期间节点地址稳定（side-table 用裸指针作 key 可行）。
- ✅ **结构完整性**：`validate_comp_unit()` 保证函数体非空、声明带初始化等。
- ✅ **SourceLoc**：全节点带，诊断与定位可用。

> 注：`ASTVisitor` 是**粗粒度**的（`visit_expr`/`visit_stmt` 为统一入口，内部需 `switch(kind)`）。对 IRGen 够用，但若 Sema 也觉得吃力，可后续一起加细粒度分派（非阻塞）。

---

## 2. IRGen 的输入需求

### 2.1 需求 A —— 标识符引用解析（name → 声明）

**为什么需要**：IRGen 遇到 `IdentExpr`（读）要生成 `load <该变量的槽>`，遇到 `AssignStmt`（写）要生成 `store <槽>`。必须知道每个 `name` 解析到**哪个具体声明**（局部 var / 局部 const / 形参 / 全局 var / 全局 const），因为：

- **局部 vs 全局**决定 IRGen 走 alloca 槽还是全局地址。
- **const** 决定是否直接内联字面量（不生成 load）。
- **块作用域屏蔽**：同名内层声明屏蔽外层，解析必须正确。

**现状**：AST 里 `IdentExpr`/`AssignStmt` 只有 `name` 字符串，无解析结果。

**硬需求还是软需求**：**软**。IRGen 可自行维护块作用域栈 + 全局符号表解析（遍历时 push/pop），但**这会重复 Sema 的作用域解析逻辑**，且两者解析结果必须一致（否则 IR 语义错）。

### 2.2 需求 B —— 常量折叠结果

**为什么需要**：

- `const` 的语义约束"必须编译期可定"要求 Sema 实现 `const_eval`（`frontend-协作说明.md` §9 已列）。
- IRGen 遇到 const 引用时，最优是**直接内联字面量**（零访问成本）。
- 全局常量初值、全局变量静态初值也需要折叠后的常量。

**现状**：`ConstDecl.init` / `GlobalConstDecl.init` 都是未折叠的 `Expr`。

**硬需求还是软需求**：**软**（对 IRGen）/ **硬**（对 Sema）。Sema 必须折叠（语义）；IRGen 可自行再折叠一次，但**重复实现折叠器，且要与 Sema 一致**。

### 2.3 需求 C —— 表达式/调用的类型

**为什么需要**：ToyC 几乎全是 `int`，唯一要区分的是 **void 函数调用**——`CallExpr` 出现在语句位置（void call，不求值结果）vs 表达式位置（int call，取结果）。IRGen 需知道 `callee` 的返回类型。

**现状**：`CallExpr.callee` 仅函数名，`Expr` 无 type 字段。

**硬需求还是软需求**：**软，且 IRGen 可轻松自解**。ToyC 函数全在 CompUnit 顶层、名字唯一，IRGen 启动时扫一遍顶层 `FuncDef` 即可建立 `func_name → return_type` 表。**这条几乎不阻塞**，IRGen 自行处理即可，除非 Sema 顺带标注更省事。

---

## 3. Sema → IRGen 信息传递机制（三方案）

若 Sema 选择向 IRGen 交付 A/B 的结果（推荐，见 §4），有三种形式：

### 方案 1：AST 加"可选标注字段"（Sema 填，Parser 不碰）

在 `ast.h` 给相关节点加默认空的标注字段，Parser 不填，Sema 遍历时填：

```cpp
struct IdentExpr {
    std::string name;
    SourceLoc loc;
    // —— Sema 标注（Parser 不碰）——
    const void* resolved = nullptr;   // 指向 VarDeclStmt / ConstDeclStmt / Param / GlobalVarDecl / GlobalConstDecl
    std::optional<int> const_value;   // 若 resolved 是 const，折叠后的值
};
struct CallExpr {
    std::string callee;
    std::vector<std::unique_ptr<Expr>> args;
    SourceLoc loc;
    const FuncDef* resolved_func = nullptr;  // Sema 填
};
```

- ✅ IRGen 遍历时**直接读字段**，最直观高效。
- ❌ **侵入 AST**（要改 `ast.h`，需前端组同意；`frontend-协作说明.md` §19 "Sema 不修改 Parser 接口"边界要放宽——可论证"加可选标注字段≠改 Parser 接口"，Parser 不填、默认空）。
- ❌ `resolved` 用 `const void*` 是 type erasure（因声明节点有 5 种不同 struct），IRGen 拿到要自己判类型；或 Sema 定义统一 `SymbolRef` 类型。

### 方案 2：Sema 产 side-table（AST 不变）✅ 推荐

Sema 遍历产出独立的结果结构，AST 保持纯净：

```cpp
// 由 Sema 产出，交给 IRGen
struct SemaResult {
    // 节点地址 → 解析结果（AST 是值语义 unique_ptr，地址稳定）
    std::unordered_map<const IdentExpr*, SymbolRef> idents;
    std::unordered_map<const AssignStmt*, SymbolRef> assigns;
    std::unordered_map<const CallExpr*, const FuncDef*> calls;
    // const 折叠值（const 声明 → 值；或 const 引用节点 → 值）
    std::unordered_map<const void*, int> const_values;
    // ... Sema 觉得有用的都放这里
};
```

- ✅ **AST 完全不被侵入**，前端组无需改 `ast.h`，§19 边界不被破坏。
- ✅ Sema/IRGen **解耦**：Sema 只交付一个 `SemaResult`，内部符号表实现自由演化。
- ✅ IRGen 查表即可，统一接口。
- ❌ IRGen 遍历时要拿着 `SemaResult` 到处查表（比方案 1 稍繁琐）。
- ⚠️ `SymbolRef` 类型需 Sema 定义（暴露给 IRGen）；节点地址作 key 依赖 AST 不被重排（当前不会）。

### 方案 3：IRGen 自解析（不依赖 Sema 内部）

IRGen 自建全局符号表 + 块作用域栈 + 常量折叠器。

- ✅ IRGen **完全自给自足**，不依赖 Sema 任何内部结果，只吃 Parser 的 AST。
- ❌ **与 Sema 重复实现**作用域解析 + 常量折叠。
- ❌ **一致性风险**：两套解析/折叠实现可能不一致 → 语义错（ToyC 规则简单，风险可控，但是真实风险）。
- ❌ Sema 检查过的东西 IRGen 再算一遍，浪费。

---

## 4. 推荐

**首选方案 2（Sema 产 `SemaResult` side-table）**，理由：AST 保持纯净（尊重前端边界）、Sema/IRGen 解耦、接口清晰统一。

**次选方案 1**（若前端组愿意接受可选标注字段）：IRGen 实现更直观，性能略好（查表 vs 读字段），但需改 `ast.h`。

**方案 3 仅作 fallback**：若 Sema 组短期无法交付结果，IRGen 可先自解析打通管线，后续再切换——但需承担一致性风险与重复工作。

> 一个折中：**A（作用域解析）走方案 2/1 交付，B（常量折叠）可由 Sema 在方案 2 的 `const_values` 里交付**，C（类型）IRGen 自扫即可（不阻塞）。

---

## 5. 请求 Sema 反馈的问题清单

请 Sema 组就以下问题给出倾向或拍板：

- **Q1（需求 A 传递方式）**：标识符解析结果，选方案 1（AST 标注）/ 方案 2（side-table）/ 方案 3（IRGen 自解析）？IRGen 倾向 **2**。
- **Q2（需求 B 传递方式）**：常量折叠结果以何种形式交付？IRGen 倾向 **方案 2 的 `const_values`**（或方案 1 的 `IdentExpr::const_value`）。是否由 Sema 负责**把对 const 的引用直接标注为字面量**？
- **Q3（需求 C）**：函数返回类型，确认 **IRGen 自扫顶层 `FuncDef` 签名** 即可（不需 Sema 专门标注）？还是 Sema 顺带在 `CallExpr` 标注？
- **Q4（`SymbolRef` 定义）**：若选方案 2，`SymbolRef` 由 Sema 定义并暴露给 IRGen。它需要携带哪些信息？IRGen 至少需要区分 `{局部var, 局部const, 形参, 全局var, 全局const}` 五类，并能定位到对应声明节点。
- **Q5（全局变量初值）**：~~关联总设计 §15 开放问题 3~~ **已拍板（前端组 2026-06-22）**：全局变量初值 **不允许** 含运行期表达式；必须编译期可求值（规则同 const 折叠：字面量 + const + 算逻运算）。Sema 报错；IRGen 按静态初值写入 `.data`。
- **Q6（交付时机）**：Sema 预计何时能交付 `SemaResult`（或等价接口）？以便 IRGen 排期。在 Sema 就绪前，IRGen 是否先用方案 3 打通 M1/M2？

---

## 6. IRGen 的承诺（便于 Sema 设计）

无论哪种方案，IRGen 侧的约束：

1. IRGen **只读** Sema 的结果（`SemaResult` 或标注字段），不修改。
2. IRGen 假定 Sema 已通过全部语义检查（未声明使用、重定义、void 误用、路径 return、break/continue 上下文等），IRGen **不做语义检查**，只消费结果。
3. IRGen 接口收敛在"AST + Sema 结果"，不依赖 Sema 内部符号表实现细节。

---

## 7. 前端组反馈（2026-06-22）

前端组已正式回复，详见 [`IR-Sema接口请求反馈-前端组.md`](./IR-Sema接口请求反馈-前端组.md)。

**摘要：**

| 问题 | 前端立场 |
|------|----------|
| Q1 | ✅ 方案 2（side-table），不改 `ast.h` |
| Q2 | ✅ Sema 经 `SemaResult::const_values` 交付 |
| Q3 | ✅ IRGen 自扫 `FuncDef`；前端提供 `build_func_signature_map()` |
| Q4 | ✅ 采用 `SymbolStorageKind`（`ast_contract.h`）+ Sema 定义 `SymbolRef` |
| Q5 | ✅ **不允许运行期表达式**；初值须编译期可求值（Sema 检查） |
| Q6 | 非前端排期 |

**前端已交付：** `ast_contract.h`、`ast_access.h`、细粒度 `ASTVisitor`、AST 生命周期约定。

**待 Sema 拍板后更新本文档状态为「接口已冻结」。**

---

## 8. ✅ 已解决：`ValueKind` 命名冲突（2026-06-24）

**原问题**：`include/toyc/ast_contract.h` 定义 `enum class ValueKind { Int, Void }`，`include/toyc/ir.h` 定义 `enum class ValueKind { Constant, GlobalAddr, BasicBlock, Function, Register, Param }`。两者同在 `namespace toyc` 且同名 —— **任何同时 include 这两个头的翻译单元编译失败**（`redefinition of 'ValueKind'`）。

**发现场景**：IRGen 实现（commit `efa792a`）需同时引用 IR 与前端契约，撞到此冲突。IRGen 已临时规避：不 include `ast_contract.h`（其 `SymbolStorageKind` 在 IRGen 当前实现中用不到，按 YAGNI 去掉）。

**影响**：
- **Sema 组**未来要用 `ast_contract.h`（`SymbolStorageKind` 在那，见 §4 Q4），与 IR 联编时会再撞 → **阻塞 Sema↔IR 集成**。

**解决方式**：
- 已将 `ast_contract.h::ValueKind` 改名为 `ValueType`。
- 已将 `value_kind_name()` 改名为 `value_type_name()`。
- Sema 与 IRGen 现在可以同时包含 `ast_contract.h` 与 `ir.h`。

**状态**：✅ 已解决，Sema↔IR 集成不再被该命名冲突阻塞。
