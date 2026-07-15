# Codegen IR 输入契约

日期：2026-06-24  
更新：2026-06-25，补充 mem2reg / optim 后的 SSA 输入形态。
范围：描述 codegen 消费 `toyc::Module` 时可依赖的 IR 形态。本文件是参考契约，不是 codegen 实施计划。

## 输入来源

默认 codegen 路径应复用现有前置阶段：

```cpp
toyc::SemaResult sema = toyc::analyze(*unit, diagnostics);
std::unique_ptr<toyc::Module> ir = toyc::generate(*unit, sema, diagnostics);
```

`Module` 类型定义在 `include/toyc/ir.h`，IR 打印逻辑在 `src/ir/ir_printer.cpp`。`-dump-ir` 输出到 `stderr`，正式汇编应输出到 `stdout`。

## 所有权和遍历

IR 对象所有权：

```text
Module
  owns GlobalVar list
  owns Function list
Function
  owns BasicBlock list
BasicBlock
  owns Instruction list
Instruction/User
  stores raw Value* operands
```

codegen 只读 IR 时，可以按如下结构遍历：

```cpp
for (const auto& global : module.globals()) { ... }
for (const auto& function : module.functions()) {
    for (const auto& block : function->blocks()) {
        for (const auto& inst : block->insts()) { ... }
    }
}
```

`Value*` 的生命周期由 `Module/Function/BasicBlock` 持有者覆盖整个 codegen 过程。不要在 codegen 中保存跨 `Module` 生命周期的裸指针。

## 命名约定

| Kind | 文本名 | 说明 |
|---|---|---|
| `ConstantInt` | `42`, `-5` | 立即数，不是指令 |
| `GlobalAddr` | `@name` | 全局对象地址，`ptr` 类型 |
| `Function` | `@name` | 函数符号 |
| `Param` | `%arg.N` | 函数参数值 |
| `Instruction` 结果 | `%v.N` | 虚拟值，全 Module 单调编号 |
| `BasicBlock` | `entry`, `bbN` | `entry` 是每个函数第一个块 |

注意：`%v.N` 编号是 Module 级全局唯一，不是每个函数重置。

## 类型集合

| `Type` | 文本 | Codegen 含义 |
|---|---|---|
| `Type::I32` | `i32` | 32 位有符号整数 |
| `Type::Ptr` | `ptr` | 指向 i32 的栈槽或全局地址 |
| `Type::Void` | `void` | 无结果 |
| `Type::Label` | `label` | 基本块目标 |

当前 IR 没有数组、结构体、浮点、真实源语言指针。所有 `ptr` 都可以按 i32 地址处理。

## 优化路径约束

`Module` 是正式 codegen 的唯一输入。`-opt` 可以改变 `Module` 的内部形态，但必须先经过 IRGen，并保持源程序的通用语义。禁止通过 AST/Sema 解释执行整个程序、预先计算 `main()` 最终返回值或构造替代原程序的最小模块。

| 路径 | 输入 | 输出 |
|---|---|---|
| 普通后端 | raw `Module` | 完整 RV32IM 汇编 |
| 优化后端 | mem2reg / optim 后的 `Module` | 完整 RV32IM 汇编 |

## 当前 IR 阶段形态

当前有三种可观察 IR 层级。

### Raw IRGen

`IRGen` 产物是非 SSA 栈式 IR：

- 每个参数和局部变量都有 `alloca i32` 栈槽。
- 参数在 `entry` 开头被 `store` 到自己的栈槽。
- 变量读写使用 `load`/`store`。
- 常量引用会被内联成 `ConstantInt`。
- 全局变量引用通过 `GlobalAddr` 做 `load`/`store`。
- 短路表达式已经变成基本块、条件跳转和临时栈槽。

默认非 `-opt` codegen 可先消费这一层，保证功能正确。

### Mem2Reg IR

`mem2reg(Module&)` 会：

- 删除可提升局部/参数槽的 `alloca/load/store`。
- 把变量当前值重写为 SSA value。
- 在控制流汇合处插入 `PhiInst`。
- 保留全局变量对应的 `GlobalAddr` load/store。

`-dump-ir -mem2reg-only` 可查看该层。codegen 若要消费这一层，必须支持 `Phi` 或先做 deSSA。

### Optim IR

`run_optim(Module&)` 当前在 mem2reg 后运行，包含：

- 常量传播 / 常量折叠。
- DCE。
- GVN / CSE。
- CFG simplify。

`-dump-ir -opt` 可查看该层。优化后 IR 仍是 SSA 风格，可能包含 `Phi`，也可能删除 alloca、load/store、空块或常量条件分支。`-opt` codegen 最终应消费这一层；在 deSSA 未落地前，可以临时诊断失败或暂时让 `-opt` 走 raw codegen，但 plan 中应把这当作过渡状态。

IR 类型系统已预留 `Phi/Shl/Shr`。后续优化落地后，codegen 还需要支持 SSA 形态或在 codegen 内部做 deSSA。

## 指令语义速查

| Opcode | 文本形态 | 语义 | 结果 |
|---|---|---|---|
| `Alloca` | `%v = alloca i32` | 分配一个 i32 栈槽 | `ptr` |
| `Load` | `%v = load p` | 读取 `*p` | `i32` |
| `Store` | `store p, v` | 写入 `*p = v` | 无 |
| `Add` | `%v = add a, b` | 有符号 32 位加法 | `i32` |
| `Sub` | `%v = sub a, b` | 有符号 32 位减法 | `i32` |
| `Mul` | `%v = mul a, b` | RV32IM 乘法 | `i32` |
| `Sdiv` | `%v = sdiv a, b` | 有符号除法，向零取整 | `i32` |
| `Srem` | `%v = srem a, b` | 有符号取余 | `i32` |
| `Neg` | `%v = neg a` | `-a` | `i32` |
| `ICmpEq` | `%v = icmp eq a, b` | `a == b`，结果 0/1 | `i32` |
| `ICmpNe` | `%v = icmp ne a, b` | `a != b`，结果 0/1 | `i32` |
| `ICmpSlt` | `%v = icmp slt a, b` | `a < b` | `i32` |
| `ICmpSgt` | `%v = icmp sgt a, b` | `a > b` | `i32` |
| `ICmpSle` | `%v = icmp sle a, b` | `a <= b` | `i32` |
| `ICmpSge` | `%v = icmp sge a, b` | `a >= b` | `i32` |
| `Br` | `br label bbN` | 无条件跳转 | 无 |
| `CondBr` | `cond_br c, label T, label F` | `c != 0` 跳 T，否则跳 F | 无 |
| `Ret` | `ret` 或 `ret v` | 函数返回 | 无 |
| `Call` | `%v = call @f, args...` | 函数调用 | `i32` 或无 |
| `Phi` | `%v = phi [v1, bb1], ...` | SSA 入边选择 | `i32` |
| `Shl` | `%v = shl a, n` | 优化产生的左移 | `i32` |
| `Shr` | `%v = shr a, n` | 优化产生的右移 | `i32` |

`Store` 的操作数顺序是指针在前、值在后：`store <ptr>, <val>`。

## 控制流约定

- `BasicBlock::terminator()` 返回块尾指令。
- `br`、`cond_br`、`ret` 是 terminator。
- 当前 IRGen 会在正常路径插入 terminator。
- `if`/`while`/短路表达式都已经降成显式块。
- `break` 直接 `br` 到循环 exit 块。
- `continue` 直接 `br` 到 while 条件块。

`BasicBlock` 有 `preds/succs` 字段，但当前构造代码没有维护完整 CFG 边列表。codegen 若需要前驱/后继，建议从 terminator 操作数现场推导，不要信任 `preds()/succs()` 已完整填充。

## 函数调用约定的 IR 侧事实

- 函数定义顺序来自源文件顺序。
- Sema 已保证调用目标在当前点可解析，并禁止调用未声明函数。
- IR `CallInst::callee_name()` 是不带 `@` 的短名。
- `CallInst::has_result()` 可判断是否有返回值。
- `CallInst` 的 operands 是实参值列表。
- ToyC 函数参数都是 `i32`。

ABI 映射属于 codegen：例如前 8 个参数到 `a0-a7`，更多参数落栈。

## 全局对象约定

`Module::globals()` 中的每个 `GlobalVar` 包含：

```cpp
GlobalAddr* addr;
ConstantInt* init;
bool is_const;
```

含义：

- `addr->label()` 是符号名，不含 `@`。
- `init->value()` 是静态初值。
- `is_const == true` 可放只读段，也可先保守放数据段。
- `is_const == false` 是可写全局变量。

当前 Sema 要求全局变量和全局常量初值可编译期求值，因此 codegen 不需要生成运行期全局初始化代码。

## Value 到机器位置的常见分类

codegen 建议先把 `Value*` 分为几类：

| Value 来源 | 典型机器位置 |
|---|---|
| `ConstantInt` | 立即数，必要时装入临时寄存器 |
| `GlobalAddr` | 全局符号地址 |
| `Param` | ABI 参数位置，或函数入口下沉后的栈槽来源 |
| `AllocaInst` 结果 | 当前函数栈槽 |
| 纯计算指令结果 | 虚拟寄存器、物理寄存器或 spill slot |
| `LoadInst` 结果 | 虚拟寄存器、物理寄存器或 spill slot |
| `CallInst` 结果 | `a0`，再绑定到目标位置 |

## 需要 codegen 自己决定的边界

- 默认路径是否直接翻译非 SSA 栈式 IR。
- `-opt` 路径是在 codegen 前做 deSSA，还是 codegen 内部边 lowering 边处理 phi。
- `alloca` 栈槽、spill 栈槽、保存寄存器区域、出参区域如何统一布局。
- 调用前后 caller-saved/callee-saved 寄存器如何保存。
- `main` 返回采用普通 `ret` 还是 exit syscall。
- 是否允许使用 RISC-V 伪指令，如 `li`, `la`, `mv`, `ret`。
