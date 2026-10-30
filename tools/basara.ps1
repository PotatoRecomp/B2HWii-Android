# Run the Windows build with one command.
# Configures if needed, builds on request or when the exe is missing, opens the log window, then plays with no time limit.
#   tools\basara.ps1            play (builds only if the exe is missing)
#   tools\basara.ps1 -Build     build first, then play
#   tools\basara.ps1 -NoLog     no log window
#   tools\basara.ps1 -TexStat   texture summary log every 2 s (BASARA_LOG_TEXSTAT)
#   tools\basara.ps1 -VPos      log ringkasan array posisi F32 tiap 2 s (BASARA_LOG_VPOS)
#   tools\basara.ps1 -DlSoft    decode display lists per vertex, without aurora indexed arrays (BASARA_DL_SOFT)
param([switch]$Build, [switch]$NoLog, [switch]$TexStat, [switch]$VPos, [switch]$DlSoft, [string]$Watch = '', [int]$Seconds = 0)
$ErrorActionPreference = 'Stop'
if ($TexStat) { $env:BASARA_LOG_TEXSTAT = '1' }
if ($VPos) { $env:BASARA_LOG_VPOS = '1' }
if ($DlSoft) { $env:BASARA_DL_SOFT = '1' }
if ($Watch) { $env:BASARA_WATCH_WRITE = $Watch }
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$bd = Join-Path $root 'generated\win-build'
$win = Join-Path $PSScriptRoot 'b-win.ps1'
$ps = "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe"

function Invoke-Win([string]$mode, [string[]]$extra = @()) {
    & $ps -NoProfile -ExecutionPolicy Bypass -File $win -Mode $mode @extra
    if ($LASTEXITCODE -ne 0) { throw "b-win.ps1 -Mode $mode failed (exit $LASTEXITCODE)" }
}

if (-not (Test-Path (Join-Path $bd 'build.ninja'))) { 'configure...'; Invoke-Win configure }
if ($Build -or -not (Test-Path (Join-Path $bd 'WiiCompiled.exe'))) { 'build...'; Invoke-Win build }

if (-not $NoLog) {
    $open = @(Get-CimInstance Win32_Process -Filter "Name='powershell.exe' OR Name='pwsh.exe'" |
        Where-Object { $_.CommandLine -like '*b-logwindow*' }).Count
    if (-not $open) {
        Start-Process $ps -ArgumentList '-NoProfile', '-NoExit', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'b-logwindow.ps1')
    }
}

'playing... (close the game window to stop)'
Invoke-Win run @('-Seconds', "$Seconds")
