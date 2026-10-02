$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLac = Join-Path $taskRoot 'build/lac.exe'
$taskTests = Join-Path $taskRoot 'build/tests'
function Test-ScopeProgram([string]$code,[string]$name,[string]$expected) {
    $exe = Join-Path $taskTests "$name.exe"
    & $taskLac $code '@' $exe
    if ($LASTEXITCODE -ne 0) { throw "Scope program compilation failed: $name" }
    $actual = (& $exe) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Scope program failed: $name [$actual] expected [$expected]" }
    $exe
}
$zeroFour = Join-Path $taskTests 'zero4.exe'
& $taskLac (Join-Path $taskRoot 'code/zero4.l') '@' $zeroFour
if ($LASTEXITCODE -ne 0) { throw 'zero4 compilation failed' }
$actual = (& $zeroFour) -join "`n"
if ($LASTEXITCODE -ne 0 -or $actual -ne "false`ntrue") { throw "zero4 failed: $actual" }
Test-ScopeProgram '{prl("bare")}' 'bare-entry' 'bare' | Out-Null
Test-ScopeProgram '(){prl("paren")}' 'paren-entry' 'paren' | Out-Null
Test-ScopeProgram 'main{fun={pr("test")} other=(){prl("!")} alias=fun alias() other() value={@7} prl(value()) prl(({@8})())}' 'local-lambdas' "test!`n7`n8" | Out-Null
Test-ScopeProgram 'fun={prl("assigned")} (){fun()}' 'top-bare-lambda' 'assigned' | Out-Null
Test-ScopeProgram 'fun=(){prl("assigned")} {fun()}' 'top-paren-lambda' 'assigned' | Out-Null
Test-ScopeProgram 'unused{prl("wrong")} main(){prl("main")}' 'main-priority' 'main' | Out-Null
$code = 'read(p&Pair):S{@p.tx} id(x:pair):pair{@x} equal(x:pair,y:pair):b{@x~y} make(){@pair(9223372036854775808123,"text")} main{pair(n:i,tx:S) p=make() q=p r=pair(&q) H=Pair(&p) t=id(r) prl(p#p) prl(p#q) prl(p~q) prl(p#H) prl(p~H) prl(t~p) prl(equal(p,p)) prl(read(&p)) prl(q.tx) prl(H.tx) prl(t.n) other=pair(1,"changed") prl(p~other)}'
$expected = "true`nfalse`ntrue`nfalse`ntrue`ntrue`ntrue`ntext`ntext`ntext`n9223372036854775808123`nfalse"
$exe = Test-ScopeProgram $code 'stack-values' $expected
$cc = if ($env:LAC_CC) { $env:LAC_CC } else { 'gcc' }
$checked = Join-Path $taskTests 'stack-values-memory.exe'
& $cc '-std=c11' '-Dmain=lac_program_main' '-IC:/msys64/ucrt64/include' "$exe.c" (Join-Path $taskRoot 'tests/allocation_check.c') 'C:/msys64/ucrt64/lib/libtommath.a' '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free' '-o' $checked
if ($LASTEXITCODE -ne 0) { throw 'Stack memory instrumentation failed' }
& $checked | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Stack fields allocation cleanup failed' }
$negatives = @{
    capture = 'main{x=1 fun={@x} prl(fun())}'
    captureParen = 'main{x=1 fun=(){@x} prl(fun())}'
    onlyLambda = 'fun={prl("no entry")}'
    mismatchedTypes = '{a(n:i) b(n:i) x=a(1) y=b(1) prl(x#y)}'
    mismatchedValues = '{a(n:i) b(n:i) x=a(1) y=b(1) prl(x~y)}'
    stackHeapName = '{a(n:i) X=a(1)}'
    heapStackName = '{a(n:i) x=A(1)}'
    storedBorrow = '{a(n:i) x=a(1) R=&x}'
    wrongReturnStorage = 'make():A{@a(1)} main{a(n:i) H=make()}'
    heapToValueParam = 'read(x:a):i{@x.n} main{a(n:i) H=A(1) prl(read(H))}'
    caseDuplicate = '{a(n:i) A(n:i)}'
}
foreach ($entry in $negatives.GetEnumerator()) {
    $message = & $taskLac $entry.Value '@' (Join-Path $taskTests "scope-fail-$($entry.Key).exe") 2>&1
    if ($LASTEXITCODE -eq 0 -or "$message" -notmatch 'error:') { throw "Invalid scope/stack program accepted: $($entry.Key)" }
}
Write-Output 'PASS: zero4, bare/parenthesized entries and lambdas, stack copies/parameters/returns, heap comparisons, cleanup and 11 diagnostics'
