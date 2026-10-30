# Realtime log window: shows the runtime console.log from the phone (tools/b-cycle.sh) or Windows (tools/b-win.ps1).
$Host.UI.RawUI.WindowTitle = 'Basara - live log'
$f = Join-Path $PSScriptRoot '..\generated\b\console-live.txt'
if (-not (Test-Path $f)) { New-Item -ItemType File $f -Force | Out-Null }
Get-Content -LiteralPath $f -Wait -Tail 40 | ForEach-Object {
    if ($_ -match 'error|fatal|abort|crash|AccessViolation|not translated|not a registered') { Write-Host $_ -ForegroundColor Red }
    elseif ($_ -match '^=====') { Write-Host $_ -ForegroundColor Cyan }
    elseif ($_ -match '^\[aurora\]') { Write-Host $_ -ForegroundColor DarkGray }
    else { Write-Host $_ }
}
