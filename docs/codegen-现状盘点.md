# Codegen 现状盘点

更新：2026-07-15，完成整程序求值合规整改。

## 当前结论

仓库已经打通完整编译链：

```text
Parser -> AST validation -> Sema -> IRGen
       -> optional mem2reg/run_optim -> RV32IM codegen
```

默认路径消费 raw IR，`-opt` 路径消费 mem2reg 与基础标量优化后的 IR。两条路径都通过同一个 `run_compiler` 驱动入口生成汇编。旧版整程序解释执行快路径已经删除。

## 已实现模块

| 模块 | 状态 | 对 codegen 的意义 |
|---|---|---|
| Lexer / Parser / AST | 已实现并测试 | 提供结构合法的编译单元 |
| Sema | 已实现并测试 | 提供标识符、赋值、调用和类型旁表 |
| IRGen | 已实现并测试 | 生成 codegen 唯一允许消费的 `Module` |
| Mem2Reg | 已实现 | 生成 SSA 值与 Phi |
| Optim | 已实现基础 pass | 常量传播、DCE、GVN/CSE、CFG simplify |
| RV32IM codegen | 已实现 | 函数、调用、全局、栈帧、Phi 边复制和大立即数 |
| 后端寄存器规划 | 部分实现 | 优化模式下覆盖基本块内短生命周期值 |
| Oracle | 已有 Docker 脚本 | 执行生成汇编并与宿主参考结果比较 |

## 当前缺口

- 缺少跨基本块全局寄存器分配、完整 spill 与 coalescing；
- 大循环的循环携带值仍会产生较多栈访存；
- LICM、强度削减、尾调用优化等尚未系统实现；
- 性能基准需要在统一 RISC-V 环境重新采集；
- 旧版整程序求值所得分数已经作废。

后续性能工作必须优化真实 IR 或生成汇编，不得预先执行源程序或按最终退出值重建 `main`。
