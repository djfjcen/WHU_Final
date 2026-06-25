# Codegen 现状盘点

日期：2026-06-24  
更新：2026-06-25，pull 后同步 mem2reg / optim 进度。
范围：给后续 codegen 计划阶段使用的事实记录。本文件不安排实施顺序，不拆任务。

## 结论

当前仓库已经打通 `Parser -> AST validation -> Sema -> IRGen -> IRPrinter`，并已新增 `mem2reg` 与基础优化 pass。`-dump-ir` 可从 ToyC 源码生成类 LLVM 文本 IR；`-dump-ir -mem2reg-only` 输出 SSA 化后的 IR；`-dump-ir -opt` 输出 mem2reg + optim 后的 IR。默认编译路径仍停在 driver，明确报 `codegen not implemented yet`。

因此 codegen 的第一接入点是 `src/driver/main.cpp` 中非 dump 路径的尾部：在 AST 校验和 Sema 成功之后，复用 `toyc::generate(*unit, sema, diagnostics)` 得到 `Module`。默认路径可先消费 raw 非 SSA IR；`-opt` 路径应消费 `mem2reg + run_optim` 后的 IR，再翻译为 RISC-V32 汇编写到 `stdout`。

性能目标需要单独看待：评测满分以 gcc -O2 运行时间为基准，朴素栈式 codegen 不足以作为最终 `-opt` 路径。ToyC 没有输入和 I/O，`main` 返回值在数学上由源码唯一决定；若能在预算内求出，`-opt` 可直接输出常量返回汇编，否则应 fallback 到优化 IR + 寄存器化后端。详见 `docs/codegen-O2满分落地参考.md`。

## 当前模块边界

| 模块 | 入口文件 | 当前状态 | 对 codegen 的意义 |
|---|---|---|---|
| Driver | `src/driver/main.cpp` | 解析 stdin 和 flags，`-dump-ir` 已接到 Sema + IRGen | 默认路径要在这里接入 codegen |
| Options | `include/toyc/options.h`, `src/driver/options.cpp` | 支持 `-opt`, `-dump-ir`, `-dump-asm`, `-mem2reg-only` | `-opt` 和 `-mem2reg-only` 已用于 dump IR |
| AST | `include/toyc/ast.h` | ToyC 文法节点已稳定 | codegen 正常不需要再读 AST |
| Sema | `include/toyc/sema.h`, `src/sema/sema.cpp` | 语义检查和 side-table 已落地 | codegen 不应重复做符号/类型解析 |
| IR | `include/toyc/ir.h`, `src/ir/ir.cpp` | `Module/Function/BasicBlock/Instruction` 已落地 | codegen 的主要输入 |
| IRBuilder | `include/toyc/ir_builder.h`, `src/ir/ir_builder.cpp` | IRGen 构造辅助已落地 | codegen 通常只读 IR，不依赖 builder |
| IRPrinter | `include/toyc/ir_printer.h`, `src/ir/ir_printer.cpp` | 文本格式已可用 | 调试 codegen 时可对照 |
| IRGen | `include/toyc/irgen.h`, `src/ir/irgen.cpp` | AST 到非 SSA IR 已落地 | codegen 默认路径可直接消费非 SSA IR |
| Mem2Reg | `include/toyc/mem2reg.h`, `src/ir/mem2reg.cpp` | alloca/load/store 提升到 SSA，产生 phi | `-opt` codegen 需要支持 phi/deSSA |
| Optim | `include/toyc/optim.h`, `src/ir/optim.cpp` | `constprop/dce/gvn/cfs/run_optim` 已落地 | `-opt` codegen 应消费优化后 IR |
| Codegen | 尚无目录 | 未实现 | 待新增模块 |

## 已验证命令

```sh
just build
```

结果：构建成功。

```sh
build/toyc-frontend-tests
```

结果：29 个 GoogleTest 用例通过。仓库规则禁止 CTest，当前测试命令符合规则。

```sh
build/toyc-ir-tests
```

结果：32 个 IR / mem2reg / optim GoogleTest 用例通过。

```sh
build/toyc-compiler -dump-ir < test/sample.tc
```

结果：成功输出 IR。`test/sample.tc` 覆盖全局 const、函数调用、局部变量、短路与 if。

```sh
build/toyc-compiler -dump-ir -mem2reg-only < test/sample.tc
```

结果：成功输出 SSA 化 IR，局部 alloca/load/store 被提升，短路临时槽变为 phi。

```sh
build/toyc-compiler -dump-ir -opt < test/sample.tc
```

结果：成功输出优化后 IR，常量条件和无用块被进一步化简。

```sh
build/toyc-compiler < test/sample.tc
```

结果：失败并输出 `codegen not implemented yet; use -dump-ast, -dump-tokens, or -dump-ir`。这是当前预期状态。

## 现有 IR 输出样例

输入：`test/sample.tc`

```llvm
@N = const i32 10

define i32 @add(i32 %arg.0, i32 %arg.1) {
entry:
  %v.0 = alloca i32
  store %v.0, %arg.0
  %v.1 = alloca i32
  store %v.1, %arg.1
  %v.2 = load %v.0
  %v.3 = load %v.1
  %v.4 = add %v.2, %v.3
  ret %v.4
}
define i32 @main() {
entry:
  %v.5 = alloca i32
  store %v.5, -5
  %v.6 = alloca i32
  store %v.6, 3
  %v.7 = alloca i32
  %v.8 = load %v.5
  %v.9 = load %v.6
  %v.10 = icmp sle %v.8, %v.9
  %v.11 = icmp ne %v.10, 0
  cond_br %v.11, label bb1, label bb2
bb1:
  %v.12 = load %v.5
  %v.13 = load %v.6
  %v.14 = call @add, %v.12, %v.13
  %v.15 = icmp ne %v.14, 0
  %v.16 = icmp ne %v.15, 0
  store %v.7, %v.16
  br label bb3
bb2:
  store %v.7, 0
  br label bb3
bb3:
  %v.17 = load %v.7
  cond_br %v.17, label bb4, label bb5
bb4:
  ret -1
bb5:
  ret 0
}
```

## 与旧文档不一致的地方

| 旧信息 | 当前事实 |
|---|---|
| README 中仍提到 CTest parser regression | `AGENTS.md` 明确禁止 CTest；当前 canonical test 是 `build/toyc-frontend-tests` |
| 总设计中 codegen/opt 目录作为骨架出现 | 当前仓库尚未创建 `src/codegen`；优化目前在 `src/ir/optim.cpp` |
| IR+优化 spec 中 AST/Sema 输入契约标为待对齐 | 现在 `SemaResult` side-table 已落地，并被 IRGen 消费 |
| 总设计中 `store(val, ptr)` 有旧表述 | 实际 IR 和 printer 使用 `store <ptr>, <val>` |
| 总设计中 `-dump-asm` 为预留 | 当前 `-dump-asm` 是 `-dump-ir` 的别名 |

## Codegen 可依赖的事实

- ToyC 只有 32 位有符号 `int` 和 `void` 函数返回。
- 源语言没有数组、指针、I/O、多文件编译。
- 全局变量和全局常量初始化值已经由 Sema 保证可编译期求值。
- `main` 必须是 `int main()`，由 Sema 检查。
- `int` 函数所有路径返回，`void` 函数 return 不带值，由 Sema 检查。
- `break/continue` 合法性由 Sema 检查。
- `void` 调用不能作为需要 `int` 的表达式，由 Sema 检查。
- IRGen 已把短路 `&&/||` 降成显式基本块和 `alloca/load/store`，codegen 不需要理解源级短路。
- `mem2reg` 会把可提升局部槽变为 SSA value，并在 merge/header 块生成 `PhiInst`。
- `run_optim` 当前包含常量传播、DCE、GVN/CSE、CFG simplify，可能删除块、折叠分支、化简 phi。

## 当前缺口

这些是事实缺口，不是执行计划：

- 没有 codegen API、目录、CMake 目标接入。
- 默认编译路径不产生汇编。
- `-opt` 已有中端优化管线，但尚未接入正式汇编输出。
- 没有面向 gcc -O2 满分目标的整程序求值、部分求值或性能基准脚本。
- 没有 deSSA、寄存器分配、spill、栈帧布局。
- 没有汇编级 oracle 回归测试脚本。
- 评测运行机制仍未从代码中体现：`main` 是普通 `ret`，还是 Linux `exit` syscall，需要后续确认。
- RISC-V 伪指令和段格式的允许范围未在仓库中固化。
