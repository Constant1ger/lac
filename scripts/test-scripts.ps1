$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLac = Join-Path $taskRoot 'build/lac.exe'
$taskTests = Join-Path $taskRoot 'build/tests'
function Test-Script([string]$source,[string]$name,[string]$expected) {
    $exe = Join-Path $taskTests "$name.exe"
    & $taskLac $source '@' $exe
    if ($LASTEXITCODE -ne 0) { throw "Script compilation failed: $name" }
    $actual = (& $exe) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Script output failed: $name [$actual]" }
    $exe
}
Test-Script (Join-Path $taskRoot 'code/zero5.l') 'zero5' "false`ntrue" | Out-Null
Test-Script 'pr("first") x=2 prl(x) y=x+3 prl(y)' 'script-order' "first2`n5" | Out-Null
Test-Script 'a(n:i,tx:S) x=a(2,"tt") H=A(&x) prl(x#H) prl(x~H)' 'script-paren-structure' "false`ntrue" | Out-Null
$exe = Test-Script 'a{n:i,tx:S} x=a(9223372036854775808123,"text") H=A(&x) q=x prl(x~H) prl(x#q) prl(q.tx) prl(q.n)' 'script-fields' "true`nfalse`ntext`n9223372036854775808123"
Test-Script 'main{a{n:i,tx:S} x=a(1,"body") x.tx.prl}' 'brace-structure-main' 'body' | Out-Null
Test-Script 'x=1; prl(x); prl((x+2));' 'script-separators' "1`n3" | Out-Null
Test-Script "import stdlib.h`nprl(abs(-7)) arr=i[3].fe{@2} prl(arr.fld(s){+s})" 'script-import' "7`n6" | Out-Null
Test-Script 'fun={prl("called")} fun()' 'script-lambda' 'called' | Out-Null
$emptySource = Join-Path $taskTests 'empty.l'
[IO.File]::WriteAllText($emptySource,'// empty script',[Text.UTF8Encoding]::new($false))
Test-Script $emptySource 'script-empty' '' | Out-Null
$cc = if ($env:LAC_CC) { $env:LAC_CC } else { 'gcc' }
$checked = Join-Path $taskTests 'script-fields-memory.exe'
& $cc '-std=c11' '-Dmain=lac_program_main' '-IC:/msys64/ucrt64/include' "$exe.c" (Join-Path $taskRoot 'tests/allocation_check.c') 'C:/msys64/ucrt64/lib/libtommath.a' '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free' '-o' $checked
if ($LASTEXITCODE -ne 0) { throw 'Script memory instrumentation failed' }
& $checked | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Script allocation cleanup failed' }
$negatives = @{
    mixed = 'prl(1) main{}'
    mixedAfter = 'main{} prl(1)'
    badField = 'a{n:i,tx:S} x=a(1,2)'
    duplicateField = 'a{n:i,n:S}'
    duplicateType = 'a{n:i} A(n:i)'
    forwardVariable = 'prl(x) x=1'
}
foreach ($entry in $negatives.GetEnumerator()) {
    $message = & $taskLac $entry.Value '@' (Join-Path $taskTests "script-fail-$($entry.Key).exe") 2>&1
    if ($LASTEXITCODE -eq 0 -or "$message" -notmatch 'error:') { throw "Invalid script accepted: $($entry.Key)" }
}
Write-Output 'PASS: zero5, sequential scripts, brace/paren structures, imports, lambdas, empty script, cleanup and 6 diagnostics'
