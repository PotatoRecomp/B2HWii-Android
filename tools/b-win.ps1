# Windows build (x64, llvm-mingw): configure | build | run | cycle (build + run).
# Flags from tools/build/NativeBuildFlags.ps1, plus -gline-tables-only for the debugger.
# run: game files through the junction game -> assets/basara-heroes, logs in UserData (portable.txt), console.log
# copied live to generated/b/console-live.txt (read by tools/b-logwindow.ps1).
# -Seconds 0 = run until the window is closed.
param(
    [ValidateSet('configure', 'build', 'run', 'cycle')][string]$Mode = 'cycle',
    [int]$Jobs = 12,
    [int]$TranslatedJobs = 6,
    [int]$Seconds = 30,
    [string]$LlvmMingw = (Join-Path $PSScriptRoot '..\third_party\llvm-mingw')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ws = $root
$tp = Join-Path $root 'third_party'
$bd = Join-Path $root 'generated\win-build'
$logs = Join-Path $root 'generated\b'
$cmake = Join-Path $tp 'cmake\bin\cmake.exe'
$ninja = Join-Path $tp 'ninja\ninja.exe'
$bin = Join-Path $LlvmMingw 'bin'
$exe = Join-Path $bd 'WiiCompiled.exe'
. (Join-Path $root 'tools\build\NativeBuildFlags.ps1')
$env:PATH = "$bin;$(Split-Path $cmake);$(Split-Path $ninja);$env:SystemRoot\System32;$env:SystemRoot"

if ($Mode -eq 'configure') {
    $a = Get-MkwNativeConfigureArguments -SourceDirectory (Join-Path $ws 'runtime') -BuildDirectory $bd `
        -Ninja $ninja -CCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang.exe') `
        -CxxCompiler (Join-Path $bin 'x86_64-w64-mingw32-clang++.exe') `
        -ResourceCompiler (Join-Path $bin 'x86_64-w64-mingw32-windres.exe') `
        -DependenciesDirectory (Join-Path $tp 'deps') `
        -AdditionalArguments @("-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs",
            '-DCMAKE_C_FLAGS=-gline-tables-only', '-DCMAKE_CXX_FLAGS=-gline-tables-only')
    $ErrorActionPreference = 'Continue'
    & $cmake @a 2>&1 | ForEach-Object { "$_" } | Out-File (Join-Path $logs 'win-configure.log') -Encoding utf8
    "configure exit $LASTEXITCODE"; exit $LASTEXITCODE
}

if ($Mode -in 'build', 'cycle') {
    $ErrorActionPreference = 'Continue'
    & $cmake --build $bd --target WiiCompiled --parallel $Jobs -- -k 0 2>&1 |
        ForEach-Object { "$_" } | Out-File (Join-Path $logs 'win-build.log') -Encoding utf8
    $code = $LASTEXITCODE
    "build exit $code"
    if ($code -ne 0) {
        Select-String -Path (Join-Path $logs 'win-build.log') -Pattern 'error:|undefined symbol' |
            Select-Object -First 5 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(240, $_.Line.Length)) }
        exit $code
    }
    $ErrorActionPreference = 'Stop'
}

if ($Mode -in 'run', 'cycle') {
    if (-not (Test-Path $exe)) { throw "not found: $exe" }
    $marker = Join-Path $bd 'portable.txt'
    if (-not (Test-Path $marker)) { 'Basara win-build: user data and logs in UserData' | Out-File $marker -Encoding ascii }
    $game = Join-Path $bd 'game'
    if (-not (Test-Path $game)) { New-Item -ItemType Junction -Path $game -Target (Join-Path $root 'assets\basara-heroes') | Out-Null }
    Get-Process WiiCompiled -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $exe } |
        ForEach-Object { Stop-Process -Id $_.Id -Force }

    $live = Join-Path $logs 'console-live.txt'
    try { Add-Content $live "===== RUN WIN $(Get-Date -Format HH:mm:ss) =====" } catch { }
    $logRoot = Join-Path $bd 'UserData\Logs'
    $before = @(Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue | ForEach-Object FullName)
    $p = Start-Process -FilePath $exe -WorkingDirectory $bd -PassThru `
        -RedirectStandardError (Join-Path $logs 'win-stderr.txt') -RedirectStandardOutput (Join-Path $logs 'win-stdout.txt')
    "game pid: $($p.Id)"

    # copy the new session console.log to console-live.txt while the process runs
    $deadline = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [datetime]::MaxValue }
    $log = $null; $pos = 0L; $runText = New-Object Text.StringBuilder
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (-not $log) {
            $dir = Get-ChildItem $logRoot -Directory -ErrorAction SilentlyContinue |
                Where-Object { $before -notcontains $_.FullName } | Sort-Object LastWriteTime | Select-Object -Last 1
            if ($dir -and (Test-Path (Join-Path $dir.FullName 'console.log'))) { $log = Join-Path $dir.FullName 'console.log' }
            continue
        }
        $fs = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        try {
            if ($fs.Length -gt $pos) {
                $fs.Position = $pos
                $buf = New-Object byte[] ($fs.Length - $pos)
                $n = $fs.Read($buf, 0, $buf.Length); $pos += $n
                $chunk = [Text.Encoding]::UTF8.GetString($buf, 0, $n)
                [IO.File]::AppendAllText($live, $chunk); [void]$runText.Append($chunk)
            }
        } finally { $fs.Dispose() }
    }
    if ($p.HasExited) { "game exited, code $($p.ExitCode)" }
    else { "still running after $Seconds s, stopped"; Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
    if ($log) { "log: $log" } else { 'console.log for this session not found' }
    $runText.ToString() -split "`n" | Select-String -Pattern 'Runtime error|fatal dialog|stopped because|Caller LR|PC=' |
        Select-Object -Last 8 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(220, $_.Line.Length)) }
}
