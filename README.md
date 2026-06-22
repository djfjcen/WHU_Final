# ToyC Compiler

ToyC → RISC-V32 编译器。武汉大学编译原理课程实践项目。

将 ToyC 语言（C 的简化子集）源程序编译为可正确执行的 RISC-V32 汇编，由在线评测系统自动评测打分。

## 现状

- **前端（M1）**：手写 Lexer / Parser / AST 已完成，支持 `-dump-tokens`、`-dump-ast`
- **语义 / IR / 代码生成**：开发中

架构设计见 [设计文档](docs/superpowers/specs/2026-06-22-toyc-compiler-design.md)。

## 技术栈

- 语言：C++20
- 构建：CMake 4.x
- 前端：手写递归下降 lexer / parser（无外部依赖）
- 目标：RISC-V32 (RV32IM)

## 接口契约

- 从 **stdin** 读 ToyC 源程序，向 **stdout** 输出 RISC-V32 汇编。
- 诊断与调试信息输出到 **stderr**。
- 可执行文件名：**`toyc-compiler`**
- 命令行参数：
  - `-opt`：开启优化 pass（性能测试时评测器会传入）。
  - `-dump-tokens` / `-lex`：输出 Token 流到 stderr。
  - `-dump-ast`：输出 AST 到 stderr。
  - `-dump-ir` / `-dump-asm`：预留，由后端阶段实现。

## 构建

```powershell
cmake -S . -B build
cmake --build build --config Debug
```

Release：`cmake --build build --config Release`

## 使用

```powershell
# 词法调试
build\Debug\toyc-compiler.exe -dump-tokens < test\sample.tc

# 语法 + AST 调试
build\Debug\toyc-compiler.exe -dump-ast < test\sample.tc

# 正式编译（代码生成完成后）
build\Debug\toyc-compiler.exe < input.tc > output.s
build\Debug\toyc-compiler.exe -opt < input.tc > output.s
```

## 测试

### Parser 回归（CTest）

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug -R parser_regression
```

或：

```powershell
powershell -ExecutionPolicy Bypass -File test/regression/parser/run_tests.ps1
```

### Oracle 对照（全链路，开发中）

ToyC 源可被 gcc 直接编译且语义等价，可将本编译器产出汇编的退出码与 gcc 编译结果比对。

## 项目结构

```
include/toyc/          公共头文件（Token、Lexer、Parser、AST、Diagnostics）
src/frontend/          词法、语法、AST、AST 遍历/打印
src/driver/            main、命令行选项、统一诊断
test/regression/parser/ Parser 回归用例
docs/                  设计文档与协作文档
```

## 文档

- [任务要求](任务要求.md) — 权威需求
- [设计文档](docs/superpowers/specs/2026-06-22-toyc-compiler-design.md) — 架构与分工
- [前端协作说明](docs/frontend-协作说明.md) — AST 接口、调试方式
- [词汇翻译表](词汇翻译表.md) — Token / 文法对照

## 里程碑

1. **M1 端到端打通** — 前端 + 最简语义 + 最简代码生成（进行中）
2. **M2 功能正确** — 完整语义与代码生成
3. **M3 IR + 优化** — SSA IR + 寄存器分配 + 核心优化
4. **M4 打磨 + 报告**
