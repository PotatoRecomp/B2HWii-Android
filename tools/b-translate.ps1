# Run the translation pipeline: translate -> manifest -> data-init -> shards.
# -ShardsOnly: emit-build-shards only (enough after adding a PPC_NATIVE_OVERRIDE in runtime/src).
param([switch]$ShardsOnly)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$ws = $root
$env:DOTNET_ROOT = Join-Path $root 'third_party\dotnet'
$env:PATH = "$env:DOTNET_ROOT;$env:PATH"
$env:WII_ALLOW_UNSUPPORTED = '1'  # 3 debugger traps at 0x8039C5A0.. are overridden natively
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'; $env:DOTNET_NOLOGO = '1'
$tr = Join-Path $root 'translator\src\Translator.Cli\bin\Release\net8.0\Translator.Cli.dll'
$logs = Join-Path $root 'generated\b'
$project = 'config/basara/recomp.yml'
function Step([string]$Name, [string[]]$A) {
    $t = Get-Date
    $ErrorActionPreference = 'Continue'
    & dotnet $tr @A 2>&1 | ForEach-Object { "$_" } | Out-File (Join-Path $logs "tr-$Name.log") -Encoding utf8
    $code = $LASTEXITCODE; $ErrorActionPreference = 'Stop'
    "{0}: exit {1}, {2:N0} s" -f $Name, $code, ((Get-Date) - $t).TotalSeconds
    if ($code -ne 0) { Get-Content (Join-Path $logs "tr-$Name.log") -Tail 15; throw "step $Name failed" }
}
Push-Location $ws
try {
  if (-not $ShardsOnly) {
    Step 'translate' @('translate-recursive', '0x8000403C', '--project', $project, '--outdir', 'generated/functions',
        '--output-metadata', 'generated/base_translation_output.json', '--production-source-bundle',
        'generated/base_translation_sources.bin', '--no-function-files', '--prune-stale', '--threads', '10')
    # The mod manifest needs RELs; Basara has none, so a failure here is skipped.
    try { Step 'manifest' @('emit-base-manifest', '--project', $project, '--out', 'build/base', '--functions-dir', 'generated/functions',
        '--translation-output-metadata', 'generated/base_translation_output.json', '--region', 'J') } catch { 'manifest skipped' }
    Step 'data-init' @('generate-data-init', '--project', $project)
  }
    Step 'shards' @('emit-build-shards', '--project', $project, '--base-metadata', 'generated/base_translation_output.json',
        '--base-functions-dir', 'generated/functions', '--native-source-dir', 'runtime/src', '--out', 'generated/build_shards')
} finally { Pop-Location }
