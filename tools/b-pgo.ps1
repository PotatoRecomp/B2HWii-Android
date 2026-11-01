# Profile-guided build of libmain.so (Android).
#   -Step gen    instrumented build in generated\android-build-pgogen, staged for the APK
#                (install it, play the battle you want optimized; counters go to
#                /sdcard/basara2heroes-beta/libmain.profraw every 30 s, or the app's data dir)
#   -Step pull   adb pull of every profraw given/found, merged into generated\b\pgo\libmain.profdata
#   -Step use    reconfigures generated\android-build with that profile (+ ThinLTO) and builds it
#   -Step off    reconfigures generated\android-build back to a plain build
param(
    [ValidateSet('gen', 'pull', 'use', 'off')][string]$Step,
    [string]$Serial = $env:BASARA_ADB_SERIAL,
    [string[]]$Profraw = @('/sdcard/basara2heroes-beta/libmain.profraw',
        '/sdcard/Android/data/com.shiinobu.basara.beta/files/libmain.profraw'),
    [switch]$NoLto,
    [string]$Ndk = (Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\28.2.13676358')
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$android = Join-Path $PSScriptRoot 'b-android.ps1'
$pgoDir = Join-Path $root 'generated\b\pgo'
$profdata = Join-Path $pgoDir 'libmain.profdata'
New-Item -ItemType Directory -Force $pgoDir | Out-Null

switch ($Step) {
    'gen' {
        & $android -Mode configure -BuildDir 'generated\android-build-pgogen' -Extra '-DMKW_PGO=generate'
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $android -Mode build -BuildDir 'generated\android-build-pgogen'
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $android -Mode stage -BuildDir 'generated\android-build-pgogen'
        'Instrumented libmain.so staged; build the APK, play, then: tools\b-pgo.ps1 -Step pull'
    }
    'pull' {
        $adb = @(); if ($Serial) { $adb = @('-s', $Serial) }
        $raws = @()
        $i = 0
        foreach ($remote in $Profraw) {
            $local = Join-Path $pgoDir ("libmain-$i.profraw"); ++$i
            if (Test-Path $remote) { Copy-Item $remote $local -Force; $raws += $local; continue }
            $ErrorActionPreference = 'Continue'
            & adb @adb pull $remote $local 2>&1 | Out-Null
            $ErrorActionPreference = 'Stop'
            if ($LASTEXITCODE -eq 0 -and (Test-Path $local)) { $raws += $local; "pulled $remote" }
        }
        if (-not $raws) { throw 'no .profraw found' }
        $tool = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-profdata.exe'
        & $tool merge -o $profdata @raws
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        Get-Item $profdata | ForEach-Object { '{0:N1} MB {1}' -f ($_.Length / 1MB), $_.FullName }
    }
    'use' {
        if (-not (Test-Path $profdata)) { throw "missing $profdata (run -Step pull)" }
        $p = [IO.Path]::GetFullPath($profdata).Replace('\', '/')
        $lto = if ($NoLto) { 'OFF' } else { 'ON' }
        & $android -Mode configure -Extra @('-DMKW_PGO=use', "-DMKW_PGO_PROFILE=$p", "-DMKW_THINLTO=$lto")
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        # ninja does not track the .profdata: objects left from an older profile carry another
        # ProfileSummary and ThinLTO refuses to link them with the new ones.
        & (Join-Path $root 'third_party\ninja\ninja.exe') -C (Join-Path $root 'generated\android-build') -t clean | Out-Null
        & $android -Mode build
        if ($LASTEXITCODE) { exit $LASTEXITCODE }
        & $android -Mode stage
    }
    'off' {
        & $android -Mode configure -Extra @('-DMKW_PGO=off', '-DMKW_THINLTO=OFF')
        'Reconfigured without PGO/LTO; run b-android.ps1 -Mode build'
    }
}
