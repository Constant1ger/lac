param([string]$Compiler = 'gcc', [string]$Toolchain = 'C:/msys64/ucrt64')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBuild = Join-Path $taskRoot 'build'
New-Item -ItemType Directory -Force -Path $taskBuild | Out-Null
$taskRuntime = (Join-Path $taskRoot 'runtime/lac_runtime.h').Replace('\', '/')
$taskSources = @('main.c', 'lexer.c', 'parser.c', 'codegen.c', 'ffi.c') | ForEach-Object { Join-Path $taskRoot "src/$_" }
$taskResource = (& "$Toolchain/bin/clang.exe" '-print-resource-dir').Trim().Replace('\', '/')
if ($LASTEXITCODE -ne 0) { throw 'Cannot find Clang resource directory' }
& $Compiler '-std=c11' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-g' '-O0' "-DLAC_RUNTIME_PATH=`"$taskRuntime`"" "-DLAC_CLANG_INCLUDE=`"$Toolchain/include`"" "-DLAC_CLANG_RESOURCE=`"$taskResource`"" "-I$Toolchain/include" @taskSources "-L$Toolchain/lib" '-lclang' '-o' (Join-Path $taskBuild 'lac.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed' }
Write-Output "Built $taskBuild/lac.exe"
