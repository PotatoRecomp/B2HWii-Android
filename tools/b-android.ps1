# Android build (arm64-v8a): configure | build | stage.
param(
    [ValidateSet('configure', 'build', 'stage')][string]$Mode = 'build',
    [int]$Jobs = 12,
    [int]$TranslatedJobs = 6,
    [string]$Ndk = (Join-Path $env:LOCALAPPDATA 'Android\Sdk\ndk\28.2.13676358'),
    # Another build tree (b-pgo.ps1 keeps the instrumented build apart) and extra -D options.
    [string]$BuildDir = 'generated\android-build',
    [string[]]$Extra = @()
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ws = $root
$tp = Join-Path $root 'third_party'
$bd = Join-Path $root $BuildDir
$logs = Join-Path $root 'generated\b'
$cmake = Join-Path $tp 'cmake\bin\cmake.exe'
$ninja = Join-Path $tp 'ninja\ninja.exe'
$deps = Join-Path $tp 'deps'
function P([string]$p) { [IO.Path]::GetFullPath($p).Replace('\', '/') }
$env:PATH = "$(Split-Path $cmake);$(Split-Path $ninja);$env:SystemRoot\System32;$env:SystemRoot"

if ($Mode -eq 'configure') {
    if (-not (Test-Path (Join-Path $Ndk 'build\cmake\android.toolchain.cmake'))) { throw "NDK not found: $Ndk" }
    $a = @('-S', (P (Join-Path $ws 'runtime')), '-B', (P $bd), '-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$(P $ninja)",
        "-DCMAKE_TOOLCHAIN_FILE=$(P (Join-Path $Ndk 'build\cmake\android.toolchain.cmake'))",
        '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
        '-DCMAKE_BUILD_TYPE=Release', '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_SDL3_PROVIDER=vendor',
        '-DCMAKE_POLICY_DEFAULT_CMP0168=NEW', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON',
        "-DMKW_TRANSLATED_COMPILE_JOBS=$TranslatedJobs") + $Extra
    foreach ($d in Get-ChildItem -LiteralPath $deps -Directory) {
        if ($d.Name -eq 'mbedtls') { $a += "-DFETCHCONTENT_SOURCE_DIR_MKW_MBEDTLS_UPSTREAM=$(P $d.FullName)"; continue }
        if ($d.Name -in @('native_prebuilt', 'cppwinrt', 'dawn_prebuilt', 'dawn_prebuilt_android')) { continue }
        $a += "-DFETCHCONTENT_SOURCE_DIR_$($d.Name.ToUpperInvariant())=$(P $d.FullName)"
    }
    $a += "-DFETCHCONTENT_SOURCE_DIR_DAWN_PREBUILT=$(P (Join-Path $deps 'dawn_prebuilt_android'))"
    $ErrorActionPreference = 'Continue'
    & $cmake @a 2>&1 | ForEach-Object { "$_" } | Out-File (Join-Path $logs 'configure.log') -Encoding utf8
    "configure exit $LASTEXITCODE"; exit $LASTEXITCODE
}
if ($Mode -eq 'build') {
    $ErrorActionPreference = 'Continue'
    & $cmake --build $bd --target WiiCompiled --parallel $Jobs -- -k 0 2>&1 |
        ForEach-Object { "$_" } | Out-File (Join-Path $logs 'build.log') -Encoding utf8
    "build exit $LASTEXITCODE"; exit $LASTEXITCODE
}
if ($Mode -eq 'stage') {
    $strip = Join-Path $Ndk 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe'
    $native = Join-Path $root 'generated\android-native'
    $libDir = Join-Path $native 'jniLibs\arm64-v8a'
    New-Item -ItemType Directory -Force $libDir | Out-Null
    & $strip --strip-unneeded -o (Join-Path $libDir 'libmain.so') (Join-Path $bd 'libmain.so')
    $hookNames = 'libhook_impl.so', 'libmain_hook.so', 'libfile_redirect_hook.so', 'libgsl_alloc_hook.so'
    Get-ChildItem $bd -Recurse -Filter '*.so' | Where-Object { $_.Name -ne 'libmain.so' -and $hookNames -notcontains $_.Name } |
        ForEach-Object { & $strip --strip-unneeded -o (Join-Path $libDir $_.Name) $_.FullName; "bundled $($_.Name)" }
    $assets = Join-Path $native 'assets\runtime'
    if (Test-Path $assets) { Remove-Item $assets -Recurse -Force }
    New-Item -ItemType Directory -Force $assets | Out-Null
    foreach ($f in 'dsp_coef.bin', 'cacert.pem') {
        $src = Join-Path $bd $f
        if (Test-Path $src) { Copy-Item $src $assets } else { "missing: $f" }
    }
    # libadrenotools hooks: loaded from a folder at run time, so they ship as assets (the app copies
    # runtime/ out of the APK) instead of jniLibs, which are not extracted.
    $hookOut = Join-Path $assets 'gpu_hooks'
    New-Item -ItemType Directory -Force $hookOut | Out-Null
    foreach ($h in $hookNames) {
        $src = Get-ChildItem $bd -Recurse -Filter $h | Select-Object -First 1
        if ($src) { & $strip --strip-unneeded -o (Join-Path $hookOut $h) $src.FullName; "hook $h" } else { "missing hook: $h" }
    }
    if (Test-Path (Join-Path $bd 'wii_bootstrap')) { Copy-Item (Join-Path $bd 'wii_bootstrap') $assets -Recurse }
    Get-Item (Join-Path $libDir 'libmain.so') | ForEach-Object { '{0:N1} MB {1}' -f ($_.Length / 1MB), $_.FullName }
}
