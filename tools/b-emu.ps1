# Starts the project-local Android emulator (third_party/android-emu, AVD "basara30").
# Log: generated/b/emu.log. adb: third_party/android-emu/platform-tools/adb.exe -s emulator-5554
param([switch]$Wipe, [switch]$Headless)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sdk = Join-Path $root 'third_party\android-emu'
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_HOME = $sdk
$env:ANDROID_AVD_HOME = Join-Path $sdk 'avd'
$env:ANDROID_USER_HOME = Join-Path $sdk 'user'
$log = Join-Path $root 'generated\b\emu.log'
$emuArgs = @('-avd', 'basara30', '-no-snapshot', '-no-boot-anim', '-gpu', 'host')
if ($Wipe) { $emuArgs += '-wipe-data' }
if ($Headless) { $emuArgs += '-no-window' }
Start-Process -FilePath (Join-Path $sdk 'emulator\emulator.exe') -ArgumentList $emuArgs `
    -RedirectStandardOutput $log -RedirectStandardError "$log.err" -WindowStyle Hidden
$adb = Join-Path $sdk 'platform-tools\adb.exe'
& $adb -s emulator-5554 wait-for-device
while ((& $adb -s emulator-5554 shell getprop sys.boot_completed 2>$null) -ne '1') { Start-Sleep 2 }
"booted"

