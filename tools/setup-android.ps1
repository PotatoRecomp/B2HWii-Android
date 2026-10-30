# Prepare the Android toolchain: download CMake, Ninja, Gradle, Dawn Android,
# the aurora dependencies (tools/build/Prepare-Dependencies.ps1) and NDK 29 (through sdkmanager).
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$tp = Join-Path $root 'third_party'
$dl = Join-Path $tp '_downloads'
New-Item -ItemType Directory -Force $dl | Out-Null

function Get-File($Uri, $Name) {
    $out = Join-Path $dl $Name
    if (-not (Test-Path $out)) {
        Write-Host "download $Name"
        Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile ($out + '.partial')
        Move-Item ($out + '.partial') $out
    }
    return $out
}
function Expand-Zip($Zip, $Dest) {
    if (Test-Path $Dest) { return }
    $tmp = $Dest + '.extract'
    if (Test-Path $tmp) { Remove-Item -Recurse -Force $tmp }
    Expand-Archive -LiteralPath $Zip -DestinationPath $tmp
    $kids = @(Get-ChildItem $tmp)
    if ($kids.Count -eq 1 -and $kids[0].PSIsContainer) { Move-Item $kids[0].FullName $Dest; Remove-Item $tmp -Recurse -Force }
    else { Move-Item $tmp $Dest }
}

# 1. CMake, Ninja, Gradle
Expand-Zip (Get-File 'https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip' 'cmake.zip') (Join-Path $tp 'cmake')
Expand-Zip (Get-File 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' 'ninja.zip') (Join-Path $tp 'ninja')
Expand-Zip (Get-File 'https://services.gradle.org/distributions/gradle-8.7-bin.zip' 'gradle.zip') (Join-Path $tp 'gradle')

# 2. Dawn prebuilt Android (pinned: v20260603.191052, dawn-android-aarch64)
$dawn = Join-Path $tp 'deps\dawn_prebuilt_android'
if (-not (Test-Path $dawn)) {
    $tgz = Get-File 'https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-android-aarch64.tar.gz' 'dawn-android-aarch64.tar.gz'
    New-Item -ItemType Directory -Force $dawn | Out-Null
    & tar -xf $tgz -C $dawn
    $kids = @(Get-ChildItem $dawn)
    if ($kids.Count -eq 1 -and $kids[0].PSIsContainer) {
        Get-ChildItem $kids[0].FullName | Move-Item -Destination $dawn
        Remove-Item $kids[0].FullName -Recurse -Force
    }
}

# 3. Aurora dependencies (the Windows-only cppwinrt step may fail)
$deps = Join-Path $tp 'deps'
try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root 'tools\build\Prepare-Dependencies.ps1') -Destination $deps
} catch { Write-Host "Prepare-Dependencies stopped: $($_.Exception.Message)" }

# 4. NDK 29 through the installed SDK sdkmanager (licenses already accepted there)
$sdk = Join-Path $env:LOCALAPPDATA 'Android\Sdk'
$ndk = Join-Path $sdk 'ndk\28.2.13676358'
if (-not (Test-Path $ndk)) {
    $sm = Get-ChildItem (Join-Path $sdk 'cmdline-tools') -Recurse -Filter sdkmanager.bat | Select-Object -First 1
    & $sm.FullName --sdk_root=$sdk 'ndk;28.2.13676358'
}
Write-Host "done"
