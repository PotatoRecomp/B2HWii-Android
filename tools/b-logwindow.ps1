# Realtime log window: shows the runtime console.log from the phone (tools/b-cycle.sh) or Windows (tools/b-win.ps1).
# The file is reopened on every poll so no handle blocks log rotation, and a shrunken file is read from the start.
$Host.UI.RawUI.WindowTitle = 'Basara - live log'
$f = Join-Path $PSScriptRoot '..\generated\b\console-live.txt'
if (-not (Test-Path $f)) { New-Item -ItemType File $f -Force | Out-Null }
$f = (Resolve-Path $f).Path

function Show-Line([string]$line) {
    if ($line -match 'error|fatal|abort|crash|AccessViolation|not translated|not a registered') { Write-Host $line -ForegroundColor Red }
    elseif ($line -match '^=====') { Write-Host $line -ForegroundColor Cyan }
    elseif ($line -match '^\[aurora\]') { Write-Host $line -ForegroundColor DarkGray }
    else { Write-Host $line }
}

$pos = [Math]::Max(0L, (Get-Item $f).Length - 8192)
$partial = ''
while ($true) {
    try {
        $fs = [IO.File]::Open($f, 'OpenOrCreate', 'Read', 'ReadWrite, Delete')
        try {
            if ($fs.Length -lt $pos) { $pos = 0L; $partial = '' }
            if ($fs.Length -gt $pos) {
                $fs.Position = $pos
                $buf = New-Object byte[] ([Math]::Min(1MB, $fs.Length - $pos))
                $n = $fs.Read($buf, 0, $buf.Length)
                $pos += $n
                $lines = ($partial + [Text.Encoding]::UTF8.GetString($buf, 0, $n)) -split "`n"
                $partial = $lines[-1]
                if ($lines.Count -gt 1) { $lines[0..($lines.Count - 2)] | ForEach-Object { Show-Line $_.TrimEnd("`r") } }
            }
        } finally { $fs.Dispose() }
    } catch { }
    Start-Sleep -Milliseconds 300
}
