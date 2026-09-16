# ida_headless.ps1 -- run IDA over the game binary with no GUI.
#
# Ghidra is free and the scripts in this repo target it, so use that if you have a choice. This is here
# because IDA's decompiler is better on this binary and if you already own a licence you will want it.
#
#   .\tools\ida_headless.ps1 -Ida "C:\Program Files\IDA Pro 9.0" -Script tools\ida_scripts\find_offsets.py
#
# THE ONE THING THAT WILL BURN YOU: IDA reuses the .i64 database next to the binary and does NOT tell you
# it is stale. After a game update you will get confident answers about last month's build. Delete the
# .i64 whenever the exe changes -- this script does it for you unless you pass -KeepDatabase.

param(
    [string]$Ida    = "C:\Program Files\IDA Pro 9.0",
    [string]$Client = "C:\Program Files (x86)\Jagex Launcher\Games\Old School RuneScape\Client\osclient.exe",
    [string]$Script = "",
    [switch]$KeepDatabase
)

# idat is the console build; ida.exe would open a window. IDA 9 renamed idat64.exe to
# idat.exe (one binary, 64-bit); accept either so older installs keep working.
$exe = Join-Path $Ida "idat.exe"
if (-not (Test-Path $exe)) { $exe = Join-Path $Ida "idat64.exe" }
if (-not (Test-Path $exe)) {
    Write-Error "neither idat.exe nor idat64.exe found under $Ida"
    exit 1
}

# Work on a COPY. Never let a tool write next to the live game install.
$work = Join-Path $PSScriptRoot "..\build\ida"
New-Item -ItemType Directory -Force -Path $work | Out-Null
$local = Join-Path $work "osclient.exe"

$needCopy = $true
if (Test-Path $local) {
    $a = (Get-FileHash $Client).Hash
    $b = (Get-FileHash $local).Hash
    $needCopy = ($a -ne $b)
    if ($needCopy) { Write-Host "game binary changed -- refreshing the copy and the database" }
}
if ($needCopy) {
    Copy-Item $Client $local -Force
    if (-not $KeepDatabase) { Remove-Item (Join-Path $work "osclient.i64") -ErrorAction SilentlyContinue }
}

$args = @("-A")                              # -A = autonomous, answer all dialogs automatically
if ($Script) { $args += "-S`"$Script`"" }
$args += $local

& $exe @args
Write-Host "done. database: $work\osclient.i64"
