param([string]$Pacman = 'C:/msys64/usr/bin/pacman.exe')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $Pacman)) { throw 'Install MSYS2 first: https://www.msys2.org/' }
& $Pacman '-S' '--needed' '--noconfirm' 'mingw-w64-ucrt-x86_64-clang' 'mingw-w64-ucrt-x86_64-libtommath'
if ($LASTEXITCODE -ne 0) { throw 'MSYS2 package installation failed' }
