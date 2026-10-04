[CmdletBinding()]
param(
    [string] $CacheDirectory = (Join-Path $PSScriptRoot "..\tools\pluginval\cache")
)

$ErrorActionPreference = "Stop"
$version = (Get-Content -LiteralPath (Join-Path $PSScriptRoot "..\tools\pluginval\VERSION") -Raw).Trim()
if ($version -ne "v1.0.4") {
    throw "Unexpected pluginval version '$version'; expected v1.0.4"
}

$platform = if ($IsMacOS) { "macos" } else { "windows" }
$asset = if ($IsMacOS) { "pluginval_macOS.zip" } else { "pluginval_Windows.zip" }
$downloadUrl = "https://github.com/Tracktion/pluginval/releases/download/v1.0.4/$asset"
$destination = Join-Path $CacheDirectory "$version\$platform"
$archive = Join-Path $destination $asset
$executable = if ($IsMacOS) {
    Join-Path $destination "pluginval.app\Contents\MacOS\pluginval"
} else {
    Join-Path $destination "pluginval.exe"
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Invoke-WebRequest -Uri $downloadUrl -OutFile $archive
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    $candidate = Get-ChildItem -LiteralPath $destination -Recurse -File |
        Where-Object { $_.Name -eq $(if ($IsMacOS) { "pluginval" } else { "pluginval.exe" }) } |
        Select-Object -First 1
    if ($null -eq $candidate) {
        throw "pluginval executable was not present in $asset"
    }
    $executable = $candidate.FullName
}

Write-Output (Resolve-Path -LiteralPath $executable).Path

