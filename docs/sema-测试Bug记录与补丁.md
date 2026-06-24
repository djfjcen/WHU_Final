# Sema 测试 Bug 记录与补丁

> 范围：语义分析、常量求值、SemaResult 与 IRGen 集成开发期间发现的问题。  
> 关联文档：[`docs/superpowers/plans/2026-06-24-sema.md`](./superpowers/plans/2026-06-24-sema.md)、[`IR-Sema接口请求反馈.md`](./IR-Sema接口请求反馈.md)。

---

## 总览

| ID | 模块 | 现象 | 严重度 | 状态 |
|----|------|------|--------|------|
| ISSUE-001 | 契约头文件 | `ast_contract.h::ValueKind` 与 `ir.h::ValueKind` 同名 | 高 | 已修复 |
| ISSUE-002 | 手动验证 | PowerShell 不支持 Bash 风格 `< input.tc` 重定向 | 低 | 已规避 |

---

## ISSUE-001：`ValueKind` 命名冲突

### 现象

Sema 需要同时包含 `ast_contract.h` 与 IRGen/IR 相关头文件时，`namespace toyc` 内出现两个 `ValueKind` 枚举定义，导致编译失败。

### 根因

前端契约层使用 `ValueKind { Int, Void }` 表示表达式值类型，IR 层已经使用 `ValueKind` 表示 IR 值类别。

### 补丁

将前端契约层枚举改名为 `ValueType`，并将 `value_kind_name()` 同步改名为 `value_type_name()`。

### 验证

完整 Debug 构建与 CTest 通过。

---

## ISSUE-002：PowerShell 输入重定向命令不可用

### 现象

按计划手动执行：

```powershell
build\Debug\toyc-compiler.exe -dump-ir < test\regression\irgen\fib.tc
```

PowerShell 报错：`The '<' operator is reserved for future use.`

### 根因

该写法是 Bash/cmd 常见输入重定向形式，当前 PowerShell 环境不接受 `<` 作为 stdin 重定向。

### 补丁

手动抽查改用管道：

```powershell
Get-Content test\regression\irgen\fib.tc | build\Debug\toyc-compiler.exe -dump-ir
Get-Content test\regression\sema\invalid\undeclared.tc | build\Debug\toyc-compiler.exe -dump-ir
```

### 验证

- `fib.tc` 成功输出递归调用 IR。
- `undeclared.tc` 返回非 0，并输出 `error [sema] 2:13: use of undeclared identifier 'x'`。

---

## 本轮测试记录

```powershell
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

结果：Debug / Release 下 `parser_regression`、`ir_tests`、`irgen_regression`、`sema_regression` 全部通过。
