# ToyC Compiler

ToyC → RISC-V32 编译器。武汉大学编译原理课程实践项目。

将 ToyC 语言（C 的简化子集）源程序编译为可正确执行的 RISC-V32 汇编，由在线评测系统自动评测打分。

## 现状

开发中。已完成架构设计（见[设计文档](docs/superpowers/specs/2026-06-22-toyc-compiler-design.md)），代码骨架待落地。

## 技术栈

- 语言：C++20
- 构建：CMake (4.0.3)
- 前端：手写递归下降 lexer / parser（无外部依赖）
- 目标：RISC-V32 (RV32IM)

## 接口契约

- 从 **stdin** 读 ToyC 源程序，向 **stdout** 输出 RISC-V32 汇编。
- 命令行参数：
  - `-opt`：开启优化 pass（性能测试时评测器会传入）。
  - `-dump-tokens` / `-dump-ast` / `-dump-ir` / `-dump-asm`：输出各阶段中间产物到 stderr（调试用）。
- 程序结果以 `main` 的返回值（进程退出码，0–255）为准。

## 构建

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## 使用

```sh
# 编译 ToyC 源文件为汇编
./build/compiler < input.tc > output.s

# 开启优化
./build/compiler -opt < input.tc > output.s
```

## 测试

采用 **Oracle 对照法**：ToyC 源可被 gcc 直接编译且语义等价，故将本编译器产出汇编运行后的退出码，与 gcc 编译同一源文件的退出码逐一比对，作为功能正确性的金标准。

```sh
# 示例（需 RISC-V 工具链，如 riscv32-unknown-elf-gcc + qemu-riscv32）
# 回归脚本见 test/（开发中）
```

## 项目结构（规划）

```
src/
  frontend/   词法、语法、AST
  sema/       符号表、类型检查、常量求值
  ir/         线性 SSA IR、IRGen、mem2reg
  opt/        优化 pass（DCE、常量传播、CSE、控制流化简、窥孔等）
  codegen/    RISC-V 指令选择、栈帧、寄存器分配、汇编输出
  driver/     main、命令行选项、诊断
test/         回归用例（.tc 源 + 期望退出码）
docs/         设计文档
```

## 文档

- [任务要求](任务要求.md) — 权威需求（ToyC 文法、语义约束、接口、评分公式）
- [设计文档](docs/superpowers/specs/2026-06-22-toyc-compiler-design.md) — 架构、IR、寄存器分配、里程碑、分工

## 里程碑

1. **M1 端到端打通** — 最小可用管线
2. **M2 功能正确** — 完整语义与代码生成
3. **M3 IR + 优化** — 线性 SSA IR + 寄存器分配 + 核心优化
4. **M4 打磨 + 报告** — 进阶优化与性能调优
