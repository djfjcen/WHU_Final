# Parser 回归测试

对 `valid/` 与 `invalid/` 下的 `.tc` 文件运行 `toyc-compiler -dump-ast`，检查退出码是否符合预期。

## 运行

```powershell
# 先构建
cmake --build build --config Debug

# Windows
powershell -ExecutionPolicy Bypass -File test/regression/parser/run_tests.ps1

# 或指定编译器路径
powershell -File test/regression/parser/run_tests.ps1 -Compiler build/Debug/toyc-compiler.exe
```

```bash
cmake --build build
bash test/regression/parser/run_tests.sh build/toyc-compiler
```

## 通过 CTest

```powershell
ctest --test-dir build -C Debug -R parser_regression
```

## 用例说明

| 目录 | 预期 | 覆盖点 |
|------|------|--------|
| `valid/` | 退出码 0 | 全局 const/var、嵌套块、if/else、while、break/continue、短路、调用、一元运算、递归 |
| `invalid/` | 退出码非 0 | 未闭合注释、缺分号、非法字符、缺 `}` |
