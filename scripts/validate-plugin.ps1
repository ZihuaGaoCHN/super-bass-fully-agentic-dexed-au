[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $PluginPath,
    [ValidateRange(1, 10)]
    [int] $Strictness = 5,
    [int] $TimeoutMilliseconds = 120000,
    [string] $OutputDirectory = (Join-Path $PSScriptRoot "..\build\validation"),
    [string] $PluginValPath
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $PluginPath)) {
    throw "Plugin path does not exist: $PluginPath"
}

$resolvedPlugin = (Resolve-Path -LiteralPath $PluginPath).Path
if ([string]::IsNullOrWhiteSpace($PluginValPath)) {
    $PluginValPath = & (Join-Path $PSScriptRoot "download-pluginval.ps1")
}
if (-not (Test-Path -LiteralPath $PluginValPath -PathType Leaf)) {
    throw "pluginval executable does not exist: $PluginValPath"
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$resolvedOutput = (Resolve-Path -LiteralPath $OutputDirectory).Path
$consoleLog = Join-Path $resolvedOutput "pluginval-console.log"
$arguments = @(
    "--strictness-level", $Strictness,
    "--timeout-ms", $TimeoutMilliseconds,
    "--output-dir", $resolvedOutput,
    "--sample-rates", "44100,48000,96000",
    "--block-sizes", "1,32,64,512,1024",
    "--validate", $resolvedPlugin
)

& $PluginValPath @arguments 2>&1 | Tee-Object -FilePath $consoleLog
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) {
    throw "pluginval failed with exit code $exitCode; see $consoleLog"
}

if (-not (Get-ChildItem -LiteralPath $resolvedOutput -File | Where-Object Length -gt 0)) {
    throw "pluginval completed without producing a validation log"
}

