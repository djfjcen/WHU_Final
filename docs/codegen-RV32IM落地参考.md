# Codegen RV32IM 落地参考

日期：2026-06-24  
更新：2026-06-25，补充 mem2reg / optim 已落地后的 codegen 消费策略。
范围：记录 ToyC IR 到 RISC-V32 汇编时需要统一的目标机事实和约定。本文件不是实施计划。

## 目标和接口

评测要求：

- 编译器从 `stdin` 读 ToyC 源码。
- 正式编译时向 `stdout` 写 RISC-V32 汇编。
- 诊断和 dump 输出到 `stderr`。
- 运行结果以 `main` 返回值，也就是进程退出码 0 到 255 为准。
- 性能测试会传入 `-opt`。满分目标不是“能跑”，而是生成代码运行时间达到 gcc -O2 基准。

目标指令集按现有设计文档记录为 RV32IM：基础整数指令加乘除模扩展。

## 性能定位

本文件记录 RV32IM codegen 细节；O2 满分策略见 `docs/codegen-O2满分落地参考.md`。这里先固化一个边界：朴素栈式翻译只能作为 correctness fallback 或 bring-up 工具，不能作为 `-opt` 的最终形态。

ToyC 的性能主路径必须是“优化后 SSA IR -> Phi 降低/寄存器化 -> RV32IM”。当前仓库已经有 `mem2reg` 和基础 `run_optim`；后续性能提升应继续完善真实 IR 优化、寄存器分配和目标机优化，不得以预先执行源程序替代代码生成。

## 待确认项

这些会影响 codegen 输出形态，但不改变上游 IR 契约：

| 项 | 影响 |
|---|---|
| 评测如何运行汇编 | 决定 `main` 末尾普通 `ret` 是否足够，还是要 Linux `exit` syscall |
| 是否接受伪指令 | 决定能否使用 `li/la/mv/ret/j/call` 等 |
| 汇编器和链接方式 | 决定段名、全局符号、入口符号细节 |
| 栈对齐要求 | RISC-V ABI 通常要求 16 字节对齐 |
| 超过 8 个实参的测试覆盖 | 决定出参栈区必须多早支持 |

## 当前落地决策

- 先不使用 RISC-V 伪指令。常量、跳转和返回都优先用基础指令表达；后续若恢复伪指令，只通过 codegen 辅助层集中切换。
- `main` 当前默认用 Linux RISC-V `exit` syscall 结束程序，避免依赖启动代码如何调用 `main`。若评测环境要求普通函数返回，可通过 `CodegenOptions::emit_exit_syscall = false` 切回。
- 全局 `const` 放入 `.rodata`，可写全局变量放入 `.data`。

## 建议采用的基础 ABI 约定

| 内容 | 约定 |
|---|---|
| 整数参数 | `a0-a7` 传前 8 个 i32 参数 |
| 返回值 | `a0` |
| 返回地址 | `ra` |
| 栈指针 | `sp` |
| 帧指针 | 可用 `s0/fp` |
| caller-saved | `t0-t6`, `a0-a7` |
| callee-saved | `s0-s11` |
| 栈对齐 | 函数调用边界 16 字节对齐 |

ToyC 没有外部库调用，所有函数都是本编译器生成，但递归和多函数调用仍要求调用约定自洽。

## 汇编文件骨架

一个保守输出通常包含：

```asm
    .section .rodata
const_or_readonly_symbols:

    .section .data
global_writable_symbols:

    .section .text
    .globl main
function_labels:
```

若评测环境不接受 `.rodata`，可以把 const 全局也放入 `.data`，功能语义仍成立，因为 ToyC 不允许写 const，Sema 已阻止。

## 全局对象映射

IR：

```llvm
@g = global i32 3
@c = const i32 10
```

可映射为：

```asm
    .section .data
    .globl g
g:
    .word 3

    .section .rodata
c:
    .word 10
```

访问全局变量时，需要先得到符号地址，再 `lw/sw`：

```asm
    la t0, g
    lw t1, 0(t0)
    sw t1, 0(t0)
```

如果禁用伪指令，需要把 `la` 展开为目标汇编器接受的 `auipc/addi` 或 `%hi/%lo` 形式。

## 栈帧内容

一个函数栈帧至少可能包含：

- 保存的 `ra`。
- 保存的 `s0/fp`，如果使用帧指针。
- 被函数使用的 callee-saved 寄存器保存区。
- IR `alloca` 对应的 i32 栈槽。
- 虚拟寄存器 spill 槽。
- 超过 `a0-a7` 的实参传递空间，或调用前临时出参空间。

建议所有槽按 4 字节分配，最终 frame size 向 16 字节对齐。

## Prologue/Epilogue 参考形态

普通函数：

```asm
func:
    addi sp, sp, -FRAME_SIZE
    sw ra, RA_OFFSET(sp)
    sw s0, FP_OFFSET(sp)
    addi s0, sp, FRAME_SIZE
    ...
.Lfunc_exit:
    lw ra, RA_OFFSET(sp)
    lw s0, FP_OFFSET(sp)
    addi sp, sp, FRAME_SIZE
    ret
```

`main` 的最终退出形式需要按评测机制确认：

```asm
    ret
```

或：

```asm
    li a7, 93
    ecall
```

第二种是 Linux RISC-V exit syscall，`a0` 保存退出码。

## 指令选择速查

| IR | RV32IM 参考 |
|---|---|
| `add a, b` | `add rd, rs1, rs2` 或 `addi` |
| `sub a, b` | `sub rd, rs1, rs2` |
| `mul a, b` | `mul rd, rs1, rs2` |
| `sdiv a, b` | `div rd, rs1, rs2` |
| `srem a, b` | `rem rd, rs1, rs2` |
| `neg a` | `sub rd, x0, rs` |
| `load p` | `lw rd, offset(base)` |
| `store p, v` | `sw rs, offset(base)` |
| `br label L` | `j L` 或 `jal x0, L` |
| `cond_br c, T, F` | `bne c, x0, T` 后接无条件跳 F |
| `ret v` | 把 `v` 放入 `a0` 后跳 epilogue |
| `ret` | 跳 epilogue |
| `call @f` | 保存必要 caller-saved 后 `call f` 或 `jal ra, f` |
| `phi` | 进入 block 前按 predecessor 插入 parallel copy，或先 deSSA |
| `shl a, n` | `slli rd, rs, n` |
| `shr a, n` | 若是算术右移用 `srai`；当前 IR 未区分逻辑/算术，需要后续约定 |

比较结果要求产生 0 或 1：

| IR 比较 | RV32IM 参考 |
|---|---|
| `icmp slt a, b` | `slt rd, a, b` |
| `icmp sgt a, b` | `slt rd, b, a` |
| `icmp sle a, b` | `slt tmp, b, a`; `xori rd, tmp, 1` |
| `icmp sge a, b` | `slt tmp, a, b`; `xori rd, tmp, 1` |
| `icmp eq a, b` | `xor tmp, a, b`; `seqz rd, tmp` 或展开 |
| `icmp ne a, b` | `xor tmp, a, b`; `snez rd, tmp` 或展开 |

若不允许 `seqz/snez` 伪指令，可用基础指令展开：

```asm
    xor tmp, lhs, rhs
    sltiu rd, tmp, 1      # eq: tmp == 0
```

```asm
    xor tmp, lhs, rhs
    sltu rd, x0, tmp      # ne: tmp != 0
```

## 立即数处理

RISC-V I 型立即数范围是 12 位有符号数。常量装载可按范围分层：

- 小立即数：直接用 `addi rd, x0, imm`。
- 大立即数：使用 `li rd, imm` 伪指令，或展开为 `lui/addi`。
- 全局地址：使用 `la rd, symbol` 伪指令，或展开成汇编器支持的重定位形式。

如果后续目标是不依赖伪指令，需要集中封装常量和地址装载逻辑。

## 调用和返回

调用前：

- 将实参 0 到 7 放入 `a0-a7`。
- 超过 8 个的实参按 ABI 放到调用者栈区。
- 保护仍需在调用后使用的 caller-saved 物理寄存器。

调用后：

- 若 IR call 有结果，结果在 `a0`。
- 若调用是 void，忽略 `a0`。
- 恢复被保护的 caller-saved 物理寄存器。

返回：

- `ret v` 需要把 `v` 放入 `a0`，再跳到统一 epilogue。
- `ret` 只跳到统一 epilogue。
- 多个 IR `ret` 建议收敛到同一个函数出口标签，便于恢复栈帧。

## 非 SSA 调试路径

当前 IR 可以不先做寄存器分配，直接用简单策略翻译：

- `alloca` 为每个结果分配一个 frame slot。
- `load` 从地址对应 slot 或全局地址读到临时寄存器，再保存结果位置。
- `store` 把值写入目标地址。
- 每个产生结果的指令都可以拥有一个结果 slot。
- 计算时用少量临时寄存器，如 `t0-t2`。

这是一种正确性优先的降低方式，性能较弱，只适合验证 codegen 语义和 ABI。面对 gcc -O2 性能目标，`-opt` 不应停在该路径；当前 mem2reg 和基础标量优化已存在，后端需要继续补齐 deSSA、跨基本块寄存器分配和目标机 peephole。

## SSA / Phi 消费

`mem2reg` 和 `run_optim` 已经会产生 SSA 风格 IR。codegen 有两个可选落地方式：

| 方式 | 说明 |
|---|---|
| codegen 前 deSSA | 在 IR 层把 phi 降成 predecessor 末尾 copy，再交给普通 lowering |
| lowering 内处理 phi | 遍历 CFG 时，根据边 `pred -> succ` 在 pred terminator 前插入/发射 copy |

无论哪种方式，都要处理 parallel copy：多个 phi 同时读取 predecessor 的旧值，不能按顺序覆盖。第一版可用临时寄存器或临时 spill slot 破环。

## Oracle 验证参考

任务说明保证 ToyC 可被 C 编译器直接编译且语义等价。汇编产物可用以下思路做对照：

1. 用本编译器生成 `.s`。
2. 用 RISC-V 工具链汇编链接运行，得到退出码。
3. 用宿主或交叉 gcc 编译同一 `.tc`，运行得到退出码。
4. 比较两者退出码。

当前仓库尚未固化 RISC-V 工具链命令，因此这里仅记录验证思路。
