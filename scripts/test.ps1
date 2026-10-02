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
$taskZeroTwo = Join-Path $taskTests 'zero2.exe'
Compile-Example (Join-Path $taskRoot 'code/zero2.l') $taskZeroTwo
$taskRandomOutput = @(& $taskZeroTwo)
if ($LASTEXITCODE -ne 0 -or $taskRandomOutput.Count -ne 2 -or $taskRandomOutput[0] -notmatch '^\d+$' -or $taskRandomOutput[1] -ne '10') { throw "zero2 output failed: $taskRandomOutput" }
if ([long]$taskRandomOutput[0] -gt (100000L * 2147483647L)) { throw 'Random sum outside rand range' }
$taskArrays = Join-Path $taskTests 'arrays.exe'
Compile-Example '(){arr=i[3] prl(arr.fld(a){+a}) arr.fe{@(.+1)} prl(arr.fld(a){+a}) prl(arr[0]) prl(i[0].fld(a){+a}) prl(i[0].fld(a=7){.+a}) prl(d[3].fe{@2d}.fld(a=1d){+a}) prl(f[2].fe{@1f}.fld(a){+a})}' $taskArrays
Expect-Output $taskArrays @() "0`n3`n1`n0`n7`n7`n2"
$taskActions = Join-Path $taskTests 'actions.exe'
Compile-Example '(){arr=i[2] arr.fe{prl(.)} prl(arr.fld(a){+a})}' $taskActions
Expect-Output $taskActions @() "0`n0`n0"
$taskForeign = Join-Path $taskTests 'foreign.exe'
Compile-Example "import stdlib.h`nimport string.h`n(){prl(abs(-7)) prl(strlen(`"abc`")) srand(1) arr=i[1000].fe{@rand} prl(arr.fld(a){+a})}" $taskForeign
$taskForeignOutput = @(& $taskForeign)
$taskForeignAgain = @(& $taskForeign)
if ($LASTEXITCODE -ne 0 -or ($taskForeignOutput -join "`n") -ne ($taskForeignAgain -join "`n") -or $taskForeignOutput[0] -ne '7' -or $taskForeignOutput[1] -ne '3') { throw 'C imports/string adapter or seeded fill failed' }
$taskConditional = Join-Path $taskTests 'conditional.exe'
Compile-Example 'f=(x:d,y:d){@x>y?x-y:y-x} (){prl(f(8d,2d)) prl(f(2d,8d)) prl(true?4:1/0) prl(false?1/0:5)}' $taskConditional
Expect-Output $taskConditional @() "6`n6`n4`n5"
$taskArrayBounds = Join-Path $taskTests 'array-bounds.exe'
Compile-Example '(){arr=i[0] prl(arr[0])}' $taskArrayBounds
$taskArrayError = & $taskArrayBounds 2>&1
if ($LASTEXITCODE -eq 0 -or "$taskArrayError" -notmatch 'out of bounds') { throw 'Numeric array bounds check failed' }
$taskStrict = & $taskLac 'f=(x:d,y:d){@x-y} (){x=128 y=138 prl(f(x,y))}' '@' (Join-Path $taskTests 'strict.exe') 2>&1
if ($LASTEXITCODE -eq 0 -or "$taskStrict" -notmatch 'expected d, got int64_t') { throw "Strict argument check failed: $taskStrict" }
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
    floatCompare = '(){prl(1d#2d)}'
    conditionalType = '(){prl(true?1:2d)}'
    fillType = '(){arr=i[3].fe{@2d}}'
    foldType = '(){prl(i[3].fld(a=0d){+a})}'
    noEntry = 'f=(x:d){@x}'
    nullLocal = '(){x=null}'
    nullCNumber = "import stdlib.h`n(){srand(null)}"
    absentImport = '(){prl(rand)}'
    movedArray = '(){arr=i[2] other=arr.fe{@1} prl(arr.sz)}'
}
foreach ($item in $taskNegatives.GetEnumerator()) {
    $taskLog = & $taskLac $item.Value '@' (Join-Path $taskTests "$($item.Key).exe") 2>&1
    if ($LASTEXITCODE -eq 0 -or "$taskLog" -notmatch 'error:') { throw "Negative case failed: $($item.Key)" }
}
Write-Output 'PASS: zero/zero2, arrays/fe/fld, empty fold, floating types, C imports, branch short circuit, strict calls and 21 compiler diagnostics'
