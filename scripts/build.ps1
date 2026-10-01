param([string]$Compiler = 'gcc')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskBuild = Join-Path $taskRoot 'build'
New-Item -ItemType Directory -Force -Path $taskBuild | Out-Null
$taskRuntime = (Join-Path $taskRoot 'runtime/lac_runtime.h').Replace('\', '/')
$taskSources = @('main.c', 'lexer.c', 'parser.c', 'codegen.c') | ForEach-Object { Join-Path $taskRoot "src/$_" }
& $Compiler '-std=c11' '-Wall' '-Wextra' '-Wpedantic' '-Werror' '-g' '-O0' "-DLAC_RUNTIME_PATH=`"$taskRuntime`"" @taskSources '-o' (Join-Path $taskBuild 'lac.exe')
if ($LASTEXITCODE -ne 0) { throw 'Compiler build failed' }
Write-Output "Built $taskBuild/lac.exe"
