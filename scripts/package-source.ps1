[CmdletBinding()]
param(
    [string] $OutputDirectory = (Join-Path $PSScriptRoot "..\dist\source"),
    [string] $Version = "1.0.1"
)

$ErrorActionPreference = "Stop"
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$stageName = "Agentic-Dexed-$Version-source"
$obsoleteStage = Join-Path $outputRoot $stageName
$obsoletePrefix = $outputRoot.TrimEnd('\') + '\'
if ($obsoleteStage.StartsWith($obsoletePrefix, [StringComparison]::OrdinalIgnoreCase) -and
    (Test-Path -LiteralPath $obsoleteStage)) {
    Remove-Item -LiteralPath $obsoleteStage -Recurse -Force
}
$workingRoot = Join-Path ([IO.Path]::GetTempPath()) ("adxs-src-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $workingRoot | Out-Null
$stage = Join-Path $workingRoot $stageName
$safePrefix = $workingRoot.TrimEnd('\') + '\'
if (-not $stage.StartsWith($safePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe source stage path: $stage"
}
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stage | Out-Null

$submoduleStatus = & git -C $repositoryRoot submodule status --recursive
if ($LASTEXITCODE -ne 0) { throw "Could not enumerate recursive submodules" }
$submodules = @()
foreach ($line in $submoduleStatus) {
    if ($line.Length -eq 0) { continue }
    if ($line[0] -ne ' ') {
        throw "Submodule is not at its pinned initialized commit: $line"
    }
    if ($line -notmatch '^[ ]([0-9a-f]{40})\s+([^\s]+)') {
        throw "Could not parse submodule status: $line"
    }
    $submodules += [ordered]@{ path = $Matches[2]; commit = $Matches[1] }
}
if ($submodules.Count -lt 6) { throw "Expected initialized recursive submodules" }

$rootPrefix = $stage.Replace('\', '/') + '/'
& git -c core.autocrlf=false -C $repositoryRoot checkout-index --all --force "--prefix=$rootPrefix"
if ($LASTEXITCODE -ne 0) { throw "Could not copy root tracked source" }
foreach ($submodule in $submodules) {
    $submoduleSource = Join-Path $repositoryRoot $submodule.path
    $submoduleDestination = Join-Path $stage $submodule.path
    New-Item -ItemType Directory -Force -Path $submoduleDestination | Out-Null
    $submodulePrefix = $submoduleDestination.Replace('\', '/') + '/'
    & git -c core.autocrlf=false -C $submoduleSource checkout-index --all --force "--prefix=$submodulePrefix"
    if ($LASTEXITCODE -ne 0) { throw "Could not copy tracked source for $($submodule.path)" }
}

$rootCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not determine source commit" }
$manifest = [ordered]@{
    product = "Agentic Dexed"
    version = $Version
    root_commit = $rootCommit
    submodules = $submodules
    build_instructions = "Documentation/BuildingAgenticDexed.md"
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content `
    -LiteralPath (Join-Path $stage "source-manifest.json") -Encoding utf8

$zip = Join-Path $outputRoot "$stageName.zip"
$tarGz = Join-Path $outputRoot "$stageName.tar.gz"
foreach ($archive in @($zip, $tarGz)) {
    if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
}
Push-Location $workingRoot
try {
    & cmake -E tar cf $zip --format=zip $stageName
    if ($LASTEXITCODE -ne 0) { throw "Could not create source zip archive" }
} finally {
    Pop-Location
}
& tar.exe -czf $tarGz -C $workingRoot $stageName
if ($LASTEXITCODE -ne 0) { throw "Could not create source tar archive" }
Remove-Item -LiteralPath $workingRoot -Recurse -Force
Write-Output "Created $zip"
Write-Output "Created $tarGz"
