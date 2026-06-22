# ToyC → RISC-V32 编译器 设计文档

- 日期：2026-06-22
- 状态：已批准，待实现
- 实现语言：C++20
- 构建系统：CMake (4.0.3)
- 权威需求：`任务要求.md`（ToyC 文法、语义约束、接口、评分公式）

## 1. 项目背景与目标

武汉大学编译原理课程实践：实现 ToyC 语言（C 的简化子集，无数组/指针/IO/多文件；本年度新增全局变量与常量）的编译器，经词法、语法、语义、代码生成，产出可正确执行的 RISC-V32 汇编。由在线评测系统自动评分。

评分（决定优化投入）：

- 评测分 = 功能分 × 75% + 性能分 × 25%；性能分以 `gcc -O2` 生成代码运行时间为基准（`min(1, 基准/实际)` 封顶）。
- 编译器自身执行效率**不计分**，编译时间宽松。
- 结果以 `main` 返回值（进程退出码，0–255）为准。

**目标定位：中庸进取** —— 引入线性 SSA IR + 寄存器分配 + 核心优化，功能与性能兼顾，时间不设限则尽力冲性能。

## 2. 实现语言与工具链

- **C++20 + CMake 4.0.3**。
- **前端手写**：递归下降 lexer + parser，**无外部依赖**（不用 Flex/Bison/ANTLR）。理由：ToyC 文法小，手写最可控、AST 自由、错误信息友好、构建最简。
- **禁用** Clang/LLVM 等现成后端框架（任务要求）。

## 3. 接口契约（对评测至关重要，不可破坏）

- 从 **stdin** 读 ToyC 源；向 **stdout** 写 RISC-V32 汇编；**stderr** 输出诊断（不计分）。
- 命令行：`compiler [-opt] [-dump-tokens|-dump-ast|-dump-ir|-dump-asm]`。
  - `-opt`：开启优化 pass 流水线（性能测试时评测会传入）。
  - `-dump-*`：开发调试，输出到 stderr。
- 本地测试：`compiler [-opt] < input.tc > output.s`。
- 运行结果以 `main` 返回值为准 → 后端须保证 `main` 的返回值正确落在 `a0` 并按评测机制成为退出码（见 §8.4）。

## 4. 总体架构与数据流

```
stdin(.tc)
  → Lexer        产出 Token 流
  → Parser       递归下降，产出 AST
  → Sema         符号表 / 类型 / 常量折叠 / 语义检查
  → IRGen        AST → 线性 IR（alloca+load/store 形式）
  → Optimizer    mem2reg(SSA) + 各优化 pass（IR→IR）
  → RegAlloc     SSA 图染色寄存器分配 + deSSA + spill
  → CodeGen      IR → RISC-V32 汇编
stdout(.s)
```

设计原则：

- **阶段可独立测试**：AST、IR 都是可打印、可独立构造的数据结构；每个 pass 是 IR→IR 的纯函数。
- **前端不依赖后端**：frontend/sema 只产 AST，不感知 IR/汇编。
- **优化与分配解耦**：优化在 SSA IR 上做，分配是独立阶段。
- **fail fast**：内部信任调用方，仅在系统边界（stdin 读取、词法语义词法错误诊断）处理异常；评测用例无语法/语义错误，但诊断能力是本地开发必需。

## 5. 模块划分（目录骨架）

```
src/
  frontend/   lexer.{h,cpp} parser.{h,cpp} ast.{h,cpp}
  sema/       symbol_table.{h,cpp} checker.{h,cpp} const_eval.{h,cpp}
  ir/         ir.{h,cpp} ir_builder.{h,cpp} ir_printer.{h,cpp} mem2reg.{h,cpp}
  opt/        dce const_prop cse control_flow algebraic loop_opts (各 .{h,cpp})
  codegen/    riscv_sel frame reg_alloc asm_printer (各 .{h,cpp})
  driver/     main.cpp options.{h,cpp} diagnostics.{h,cpp}
test/         regression/ (自建 .tc 用例 + 期望退出码)
CMakeLists.txt
```

模块边界即编译边界：每个目录职责单一，对外暴露最小接口头文件，内部可独立演化。

## 6. IR 设计（路线 A：alloca + mem2reg → SSA）

### 6.1 结构

层次：`Module > Function > BasicBlock > Instruction`。

- **Function**：形参列表（虚拟寄存器）、基本块链、`entry` 块。
- **BasicBlock**：有序指令表，以唯一 terminator 结尾（`br`/`cond_br`/`ret`）。
- **Instruction**：SSA 值，每个结果有唯一虚拟寄存器名 `%v.N`。

### 6.2 指令类别

- 二元算逻：`add/sub/mul/div/rem`、`and/or`、`shl/shr`（优化产生）。
- 比较：`cmp(eq,ne,lt,le,gt,ge)`，结果 0/1。
- 一元：`neg`、`lnot`（逻辑非）。
- 控制流：`br(label)`、`cond_br(cond, then, else)`、`ret(val?)`。
- 内存：`alloca`、`load(ptr)`、`store(val, ptr)`。
- 调用：`call(func, args...)`，结果在 `a0`。
- `copy(dst, src)`（deSSA 后 phi 消除产生）。
- `phi(incoming: [(block, value)...])`（SSA 阶段）。
- 常量用立即数操作数表示（非指令）。

### 6.3 IRGen（AST → IR）

- 每个局部变量在 `entry` 块生成一条 `alloca`（栈槽）。
- 变量读写 → `load`/`store` 该槽。
- 表达式按 AST 递归求值，产生 SSA 值；短路求值（`&&`/`||`）用基本块 + `cond_br` 显式建模。
- 全局变量/常量登记到 Module，引用通过地址 `load`（常量可在前端常量折叠时直接内联立即数）。
- 控制流（if/while/break/continue）用基本块 + 显式跳转；break/continue 跳到循环的出口/步进块。

此阶段产物**非 SSA**（变量经 alloca/load/store），SSA 化交给 mem2reg。

### 6.4 mem2reg（→ SSA）

经典支配 frontier 算法：

1. 计算支配树与支配边界。
2. 对每个可提升的 alloca，在支配边界插入 phi。
3. 重命名：支配树 DFS，用版本号维护当前值，phi 入边在后续填值。
4. 删除被完全提升的 alloca 及其 load/store。

未能提升的变量（ToyC 实际上没有取地址操作，理论上全部可提升）保留 alloca/load/store。

### 6.5 deSSA

寄存器分配前或其中，将 phi 转为 `copy`（必要时在临界边上插入临时块）。SSA 下图染色完成后，并行 copy 用干涉图约束处理（简化：顺序插入 copy，必要时加临界边拆分）。

## 7. 寄存器分配

- **SSA 上图染色**（简化 Chaitin-Briggs）。SSA 干涉图更小、更易染色（SSA 中值在定义前不被使用，干扰边少）。
- 工作流程：构建活跃变量 → 构建干涉图 → 简化（压栈）→ 选择（弹栈着色）→ 溢出处理。
- 溢出：把溢出变量的虚拟寄存器分配到栈槽，在其使用点前后插入 `load`/`store`，重新跑分配直到收敛。
- 调用约定遵循 §8.2，被调用者保存寄存器（`s0–s11`）在函数 prologue/epilogue 保存恢复。

## 8. 代码生成（RISC-V32 / RV32IM）

### 8.1 指令集

RV32IM：整数基础指令 + `mul/div/rem`（`%` 运算符需要 `rem`）。无浮点（ToyC 只有 int）。

### 8.2 ABI 与栈帧

- 参数：`a0–a7` 传参（ToyC 参数少，一般够用），超出落栈。
- 返回值：`a0`。
- 栈帧：`ra`、`fp` 进函数保存；局部溢出/alloca 变量、被调用者保存寄存器保存区在栈。
- `sp` 16 字节对齐。

### 8.3 全局数据

- 全局变量 → `.data`，带初值（初值需可静态确定；若初值含运行期表达式，则需 `.bss` 置零 + 运行期初始化代码——见开放问题）。
- 全局常量 → `.rodata`（只读）或 `.data`，初值由前端常量折叠确定。

### 8.4 main 的 epilogue（可配置，应对未知评测）

- 默认：`main` 末尾将返回值置于 `a0` 后 `ret`。
- 变体：`main` 返回后发 Linux exit syscall（`li a7, 93; ecall`，`a0`=返回值）。
- **列为首要调研项**，问清助教评测运行机制后二选一。设计上把 epilogue 收敛到 codegen 一处，切换成本低。

### 8.5 输出

带注释、人眼可读的汇编（标签、寄存器、立即数清晰），便于调试与对照。

## 9. 前端与语义

### 9.1 Lexer

Token 类型：关键字（`int/void/const/if/else/while/break/continue/return`）、`ID`、`NUMBER`（正则 `-?(0|[1-9][0-9]*)`）、运算符界符。跳过空白与 `//`、`/* */` 注释。

### 9.2 Parser（递归下降）

- 左结合优先级用循环处理左递归产生式（`AddExpr/MulExpr/LAnd/LOr/Rel`）。
- 其余按文法直译，构造 AST。
- AST 节点：`CompUnit / Decl(Const,Var) / FuncDef / Param / Block / 各 Stmt / 各 Expr`。

### 9.3 语义分析

- **符号表**：作用域栈，块作用域屏蔽外层同名符号；函数全局唯一。
- **检查清单**：重定义、未声明使用、函数须声明后调用（允许递归）、`int` 函数所有执行路径须 `return`、void 函数 `return` 不得带值、void 函数调用不得作 if/while 条件或赋值右值、break/continue 须在循环内。
- **const 编译期求值**：常量折叠器只接受数字字面量 + 已声明常量 + 算逻运算，编译期求出常量值（满足"const 必须编译期可确定"约束，且用于全局常量初值与全局变量静态初值）。
- 诊断输出到 stderr（带行号）。

## 10. 优化 pass 流水线（`-opt` 时启用）

核心（必做），顺序：

1. `mem2reg`（SSA 提升）
2. 常量折叠 / 传播
3. 死代码消除（DCE）
4. 公共子表达式消除（GVN/CSE）
5. 控制流简化（合并基本块、删除不可达块、常量条件分支折叠）
6. 代数化简与窥孔（`x+0`、`x*1`、`x*2^n→shl` 等仅当更优）
7. 寄存器分配

进阶（时间允许，冲性能）：循环不变量外提、强度削弱、死存储消除、小函数内联（ToyC 函数小收益高，递归须限制深度）。

## 11. 测试与验证（最大杠杆）

**Oracle 对照法**：任务文档保证 ToyC 源可被 gcc 直接编译、语义等价。

- 功能验证：我们的 `.s` → 汇编链接运行取退出码 ↔ `gcc` 编译同一 `.tc` 运行的退出码，逐用例比对。这是最强的功能正确性金标准。
- 回归集：`test/regression/` 自建 `.tc` 用例（覆盖全部文法/语义特性：短路求值、嵌套作用域、全局/局部常量、递归、循环等），配期望退出码脚本。
- 性能基准：用 `gcc -O2` 计时作基准（与评分基准一致），自测性能分。
- 阶段 dump：`-dump-*` 各阶段产物人眼/脚本检查。

## 12. 里程碑

| 阶段 | 目标 | 后端状态 |
|---|---|---|
| **M1 端到端打通** | lexer+parser+AST + 最简语义 + 最简代码生成；能编 `int main(){return 常量;}` 并产出可运行汇编 | 朴素栈式，无优化 |
| **M2 功能正确** | 完整语义 + 完整代码生成（控制流/函数调用/全局变量与常量）；功能测试全过 | 朴素局部寄存器分配 |
| **M3 IR + 优化** | 引入线性 SSA IR，代码生成迁到 IR；上核心优化 + 图染色寄存器分配 | 性能分起飞 |
| **M4 打磨 + 报告** | 进阶优化、性能调优、实践报告 | — |

每个里程碑以「Oracle 对照回归集全过」为完成判据。

## 13. 分工建议（4 人）

1. **前端**：lexer / parser / AST。
2. **语义分析**：符号表 / 检查 / 常量求值。
3. **IR + 优化**：IR 定义 / IRGen / mem2reg / 各优化 pass。
4. **代码生成 + 寄存器分配**：指令选择 / 栈帧 / 图染色 / 汇编输出。

M1 四人合力先打通管线（前端+最简sema+最简codegen），再各守一摊。IR/优化与代码生成有依赖，M3 需协调接口。

## 14. 风险与对策

- **评测运行环境未知** → epilogue 可配置（§8.4）+ 首要调研；输出保守标准 RV32IM 汇编，避免冷门伪指令。
- **SSA / mem2reg 复杂** → M2 先用非 SSA 栈式保功能（降级回退路径明确），M3 再上 SSA。
- **寄存器分配 spill bug** → 充分 Oracle 对照回归测试。
- **全局变量初值含运行期表达式** → 任务要求所有声明必须带初始化，但全局变量初值若含函数调用等非常量，需运行期初始化（`.bss` 置零 + main 前置初始化或专用 init）；倾向先用"全局初值限定为常量折叠可求值"假设，调研确认。

## 15. 开放问题（待确认，影响实现细节但不阻塞架构）

1. 评测运行机制（模拟器直取 a0 / exit syscall / 链接 libc）→ 决定 epilogue 形式（§8.4）。
2. 允许的指令子集、伪指令、段格式（`.data/.rodata/.bss/.text` 是否都支持）。
3. 全局变量初值是否允许含运行期（非常量）表达式。
4. 评测 RISC-V 工具链版本（影响汇编语法细节，如寄存器名、立即数范围）。

这些以问清助教为准；架构上已留可配置点，不阻塞开发启动。
