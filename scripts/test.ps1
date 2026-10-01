$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLac = Join-Path $taskRoot 'build/lac.exe'
$taskTests = Join-Path $taskRoot 'build/tests'
New-Item -ItemType Directory -Force -Path $taskTests | Out-Null
function Compile-Example([string]$source, [string]$target) {
    & $taskLac $source '@' $target
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed: $source" }
}
function Expect-Output([string]$exe, [string[]]$programArgs, [string]$expected) {
    $actual = (& $exe @programArgs) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $actual -ne $expected) { throw "Unexpected output: [$actual], expected [$expected]" }
}
$taskZero = Join-Path $taskTests 'zero.exe'
Compile-Example (Join-Path $taskRoot 'code/zero.l') $taskZero
Expect-Output $taskZero @('4', '5') "falsetrue`nx = 4, y = 5"
Expect-Output $taskZero @('abc', '12junk') "falsetrue`nx = 0, y = 12"
$taskBounds = & $taskZero 4 2>&1
if ($LASTEXITCODE -eq 0 -or "$taskBounds" -notmatch 'out of bounds') { throw 'Bounds check failed' }
$taskInline = Join-Path $taskTests 'inline space.exe'
Compile-Example 'main(A){prl("hello") prl(i(" -12tail"))}' $taskInline
Expect-Output $taskInline @() "hello`n-12"
$taskLogical = Join-Path $taskTests 'logical.exe'
Compile-Example 'effect(n:i):b{prl(n) @true} main(A){prl(false&effect(1)) prl(true|effect(2)) prl(true&effect(3))}' $taskLogical
Expect-Output $taskLogical @() "false`ntrue`n3`ntrue"
$taskEscapes = Join-Path $taskTests 'escapes.exe'
Compile-Example 'main(A){prl("\{value\}={2+3}")}' $taskEscapes
Expect-Output $taskEscapes @() '{value}=5'
$taskNegatives = @{
    unknown = 'main(A){prl(missing)}'
    copy = 'main(A){P=Point(x=1,y=2) Q=Point(P)}'
    casing = 'main(A){p=Point(x=1,y=2)}'
    moved = 'main(A){P=Point(x=1,y=2) Q=P prl(P.x)}'
    duplicateEntry = 'main(A){} m(A){}'
    badReturn = 'f():i{@"wrong"} main(A){}'
    capture = 'main(A){x=1 f=(){@x}}'
    badComment = 'main(A){ /* never closes'
    stringMove = 'main(A){s="abc" t=s prl(s)}'
    unsigned = 'f():u{@1} main(A){}'
    duplicateField = 'main(A){P=Point(x=1,x=2)}'
    heapPrimitive = 'main(A){X=1}'
}
foreach ($item in $taskNegatives.GetEnumerator()) {
    $taskLog = & $taskLac $item.Value '@' (Join-Path $taskTests "$($item.Key).exe") 2>&1
    if ($LASTEXITCODE -eq 0 -or "$taskLog" -notmatch 'error:') { throw "Negative case failed: $($item.Key)" }
}
Write-Output 'PASS: zero, atoi parsing, bounds, inline source, spaced paths, short-circuit logic, interpolation and 12 compiler diagnostics'
