param(
    [string]$Compiler = ""
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

if ($Compiler -eq "") {
    $repoRoot = Resolve-Path (Join-Path $root "..\..\..")
    $Compiler = Join-Path $repoRoot "build\Debug\toyc-compiler.exe"
    if (-not (Test-Path $Compiler)) {
        $Compiler = Join-Path $repoRoot "build\Release\toyc-compiler.exe"
    }
}

if (-not (Test-Path $Compiler)) {
    Write-Error "Compiler not found: $Compiler"
}

cmake -DCOMPILER="$Compiler" -P (Join-Path $root "run_tests.cmake")
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
