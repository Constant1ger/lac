$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLac = Join-Path $taskRoot 'build/lac.exe'
$taskTests = Join-Path $taskRoot 'build/tests'
New-Item -ItemType Directory -Force -Path $taskTests | Out-Null
$testExe = Join-Path $taskTests 'declared-test.exe'
& $taskLac (Join-Path $taskRoot 'code/test.l') '@' $testExe
if ($LASTEXITCODE -ne 0) { throw 'test.l compilation failed' }
$actual = (& $testExe) -join "`n"
if ($LASTEXITCODE -ne 0 -or $actual -ne 'tt') { throw "test.l output failed: $actual" }
$code = 'text(p&A):S{@p.tx} main(){A(n:i,tx:S,v:d,ratio:f,ok:b) s="hello" B=A(922337203685477580812345,s,2d,3f,true) C=A(&B) D=A(n=1,tx="world",v=2d,ratio=3f,ok=false) prl(B.n) prl(B.tx) prl(B.v) prl(B.ratio) prl(B.ok) prl(B~C) prl(B#C) prl(B~D) prl(text(&C)) prl(s) prl("field={B.tx}")}'
$exe = Join-Path $taskTests 'declared-fields.exe'
& $taskLac $code '@' $exe
if ($LASTEXITCODE -ne 0) { throw 'Typed fields compilation failed' }
$expected = "922337203685477580812345`nhello`n2`n3`ntrue`ntrue`nfalse`nfalse`nhello`nhello`nfield=hello"
$actual = (& $exe) -join "`n"
if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Typed fields output failed: $actual" }
$checked = Join-Path $taskTests 'declared-fields-memory.exe'
$cc = if ($env:LAC_CC) { $env:LAC_CC } else { 'gcc' }
& $cc '-std=c11' '-Dmain=lac_program_main' '-IC:/msys64/ucrt64/include' "$exe.c" (Join-Path $taskRoot 'tests/allocation_check.c') 'C:/msys64/ucrt64/lib/libtommath.a' '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free' '-o' $checked
if ($LASTEXITCODE -ne 0) { throw 'Typed fields memory instrumentation failed' }
& $checked | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Typed fields allocation cleanup failed' }
$negatives = @{
    wrongField = '(){A(n:i,tx:S) B=A("bad","text")}'
    wrongCount = '(){A(n:i,tx:S) B=A(1)}'
    duplicateType = '(){A(n:i) A(n:i)}'
    duplicateField = '(){A(n:i,n:S)}'
    mixedDeclaration = '(){A(n:i,tx="text")}'
    lowerType = '(){a(n:i)}'
    copyBorrow = '(){A(tx:S) B=A("text") C=A(B)}'
    missingField = '(){A(tx:S) B=A("text") B.bad.pr}'
    movedStruct = '(){A(tx:S) B=A("text") C=B B.tx.pr}'
    fieldOrder = '(){A(n:i,tx:S) B=A(tx="text",n=1)}'
    nestedOwner = '(){A(n:i) B(a:A)}'
}
foreach ($entry in $negatives.GetEnumerator()) {
    $message = & $taskLac $entry.Value '@' (Join-Path $taskTests "struct-fail-$($entry.Key).exe") 2>&1
    if ($LASTEXITCODE -eq 0 -or "$message" -notmatch 'error:') { throw "Invalid structure accepted: $($entry.Key)" }
}
Write-Output 'PASS: test.l, typed fields/constructors, string copy/return/equality, allocation cleanup and 11 invalid structure cases'
