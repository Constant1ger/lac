$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path $PSScriptRoot -Parent
$taskLac = Join-Path $taskRoot 'build/lac.exe'
$taskTests = Join-Path $taskRoot 'build/tests'
New-Item -ItemType Directory -Force -Path $taskTests | Out-Null
function Test-BigProgram([string]$code, [string]$name, [string[]]$expected) {
    $exe = Join-Path $taskTests "$name.exe"
    & $taskLac $code '@' $exe
    if ($LASTEXITCODE -ne 0) { throw "Bignum compilation failed: $name" }
    $actual = @(& $exe)
    if ($LASTEXITCODE -ne 0 -or ($actual -join "`n") -ne ($expected -join "`n")) { throw "Bignum output failed: $name [$actual] expected [$expected]" }
}
$huge = [bigint]::Pow(10,80) + 123456789
$second = [bigint]::Pow(10,39) + 98765
$rng = [Random]::new(73)
$pairs = @(
    @([bigint]::Parse('9223372036854775800'), [bigint]::Parse('223372036854775807')),
    @([bigint]::Parse('-9223372036854775808'), [bigint](-1)),
    @($huge, $second), @(-$huge, $second), @($huge, -$second), @(-$huge, -$second),
    @([bigint]::Parse('-9223372036854775808'), [bigint]::Parse('-9223372036854775808'))
)
for ($j=0; $j -lt 8; $j++) {
    $a = [bigint]::Pow(10, $rng.Next(20,90)) + $rng.Next(1,100000)
    $b = [bigint]::Pow(10, $rng.Next(1,45)) + $rng.Next(1,100000)
    if ($j % 2) { $a = -$a }
    if ($j % 3) { $b = -$b }
    $pairs += ,@($a,$b)
}
$code = '(){'
$expected = @()
foreach ($pair in $pairs) {
    [bigint]$a = $pair[0]; [bigint]$b = $pair[1]
    $code += "prl(($a)+($b)) prl(($a)-($b)) prl(($a)*($b)) prl(($a)/($b)) prl(($a)%($b)) prl(($a)>($b)) prl(($a)~($a)) "
    $expected += ($a+$b).ToString(),($a-$b).ToString(),($a*$b).ToString(),([bigint]::Divide($a,$b)).ToString(),([bigint]::Remainder($a,$b)).ToString(),($a -gt $b).ToString().ToLowerInvariant(),'true'
}
$code += '}'
Test-BigProgram $code 'big-arithmetic' $expected
Test-BigProgram "id(x:i):i{@x} twice(x:i,y:i){@x+y} main(){a=$huge b=a prl(id(b)) prl(twice(a,a)) prl(a) prl(d(100000000000000000000))}" 'big-values' @($huge.ToString(),($huge*2).ToString(),$huge.ToString(),'1e+20')
Test-BigProgram "(){a=i[3].fe{@$huge} prl(a[0]) prl(a.fld(s){+s}) a.fe{@(.+1)} prl(a[2]) prl(a.fld(s=1){+s}) prl(i[0].fld(s=$huge){+s})}" 'big-array' @($huge.ToString(),($huge*3).ToString(),($huge+1).ToString(),($huge*3+4).ToString(),$huge.ToString())
Test-BigProgram "read(p&Point):i{@p.x} main(){P=Point(x=$huge,y=-$huge) Q=Point(&P) prl(P~Q) prl(read(&Q)) prl(P.x) prl(true?P.x:Q.y) prl(false?P.x:Q.y)}" 'big-structure' @('true',$huge.ToString(),$huge.ToString(),$huge.ToString(),(-$huge).ToString())
Test-BigProgram "(){prl(i(`" +${huge}tail`")) prl(i(`" -${huge}tail`")) prl(i(`"no digits`")) prl(`"number={$huge}`") prl(($huge)-($huge))}" 'big-text' @($huge.ToString(),(-$huge).ToString(),'0',"number=$huge",'0')
$exitExe = Join-Path $taskTests 'big-exit.exe'
& $taskLac 'main(){@7}' '@' $exitExe
if ($LASTEXITCODE -ne 0) { throw 'Exit program compilation failed' }
& $exitExe
if ($LASTEXITCODE -ne 7) { throw 'Integer return to exit code failed' }
$failures = @{
    zeroDivide = @("(){prl(($huge)/0)}",'division by zero')
    zeroRemainder = @("(){prl(($huge)%0)}",'division by zero')
    arraySize = @("(){a=i[$huge]}",'signed 64-bit boundary')
    arrayIndex = @("(){a=i[1] prl(a[$huge])}",'signed 64-bit boundary')
    cSigned = @("import stdlib.h`n(){prl(abs(2147483648))}",'C argument range')
    cUnsigned = @("import stdlib.h`n(){srand(4294967296)}",'C argument range')
    cBig = @("import stdlib.h`n(){srand($huge)}",'unsigned 64-bit boundary')
    cNegative = @("import stdlib.h`n(){srand(-1)}",'negative integer')
    exitRange = @("main(){@$huge}",'signed 64-bit boundary')
}
foreach ($entry in $failures.GetEnumerator()) {
    $exe = Join-Path $taskTests "big-fail-$($entry.Key).exe"
    & $taskLac $entry.Value[0] '@' $exe
    if ($LASTEXITCODE -ne 0) { throw "Boundary case did not compile: $($entry.Key)" }
    $message = & $exe 2>&1
    if ($LASTEXITCODE -eq 0 -or "$message" -notmatch $entry.Value[1]) { throw "Boundary failure: $($entry.Key): $message" }
}
foreach ($name in @('big-values','big-array','big-structure','big-text','big-arithmetic')) {
    $generated = Join-Path $taskTests "$name.exe.c"
    $checked = Join-Path $taskTests "$name-memory.exe"
    $cc = if ($env:LAC_CC) { $env:LAC_CC } else { 'gcc' }
    & $cc '-std=c11' '-Dmain=lac_program_main' '-IC:/msys64/ucrt64/include' $generated (Join-Path $taskRoot 'tests/allocation_check.c') 'C:/msys64/ucrt64/lib/libtommath.a' '-Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free' '-o' $checked
    if ($LASTEXITCODE -ne 0) { throw "Memory instrumentation failed: $name" }
    & $checked | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Allocation cleanup failed: $name" }
}
Write-Output 'PASS: bignum arithmetic against .NET BigInteger, copies/returns, arrays/fold, structures, text and checked C/index/exit boundaries'
