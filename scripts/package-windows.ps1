[CmdletBinding()]
param(
    [string] $BuildDirectory = (Join-Path $PSScriptRoot "..\build\windows"),
    [string] $OutputDirectory = (Join-Path $PSScriptRoot "..\dist\windows"),
    [string] $Version = "1.0.1",
    [switch] $Unsigned,
    [switch] $SkipStandalone
)

$ErrorActionPreference = "Stop"
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$buildRoot = [IO.Path]::GetFullPath($BuildDirectory)
if (-not (Test-Path -LiteralPath $buildRoot -PathType Container)) {
    throw "Build directory does not exist: $buildRoot"
}

$artifacts = Join-Path $buildRoot "Source\AgenticDexed_artefacts\Release"
$vst3 = Join-Path $artifacts "VST3\Agentic Dexed.vst3"
$standalone = Join-Path $artifacts "Standalone\Agentic Dexed.exe"
if (-not (Test-Path -LiteralPath $vst3 -PathType Container)) {
    throw "Release VST3 bundle does not exist: $vst3"
}
if (-not $SkipStandalone -and -not (Test-Path -LiteralPath $standalone -PathType Leaf)) {
    throw "Release Standalone executable does not exist: $standalone"
}

$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null
$stageName = "Agentic-Dexed-$Version-windows-x64"
$stage = Join-Path $outputRoot $stageName
$safePrefix = $outputRoot.TrimEnd('\') + '\'
if (-not $stage.StartsWith($safePrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Unsafe package stage path: $stage"
}
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

New-Item -ItemType Directory -Force -Path (Join-Path $stage "VST3") | Out-Null
Copy-Item -LiteralPath $vst3 -Destination (Join-Path $stage "VST3") -Recurse
if (-not $SkipStandalone) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stage "Standalone") | Out-Null
    Copy-Item -LiteralPath $standalone -Destination (Join-Path $stage "Standalone\Agentic Dexed.exe")
}
foreach ($document in @("LICENSE", "THIRD_PARTY_NOTICES.md", "README.md")) {
    Copy-Item -LiteralPath (Join-Path $repositoryRoot $document) -Destination (Join-Path $stage $document)
}

function Assert-X64Pe([string] $Path, [string] $Label) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Packaged $Label binary is missing: $Path"
    }
    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 256 -or $bytes[0] -ne 0x4d -or $bytes[1] -ne 0x5a) {
        throw "Packaged $Label is not a Windows PE image"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    $machine = [BitConverter]::ToUInt16($bytes, $peOffset + 4)
    if ($machine -ne 0x8664) {
        throw ("Packaged $Label has PE machine 0x{0:x4}; expected x86_64 0x8664" -f $machine)
    }
    return "$Label PE machine: x86_64 (0x8664)"
}

$architectureReport = @(
    Assert-X64Pe `
        (Join-Path $stage "VST3\Agentic Dexed.vst3\Contents\x86_64-win\Agentic Dexed.vst3") `
        "VST3"
)
if (-not $SkipStandalone) {
    $architectureReport += Assert-X64Pe `
        (Join-Path $stage "Standalone\Agentic Dexed.exe") `
        "Standalone"
}
Set-Content -LiteralPath (Join-Path $stage "architecture.txt") -Value $architectureReport -Encoding utf8

$gitCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw "Could not determine the source commit" }
& cmake `
    "-DOUTPUT_FILE=$(Join-Path $stage 'manifest.json')" `
    "-DVERSION=$Version" `
    "-DPLATFORM=windows" `
    "-DARCHITECTURES=x86_64" `
    "-DGIT_COMMIT=$gitCommit" `
    -P (Join-Path $repositoryRoot "packaging\PackageManifest.cmake")
if ($LASTEXITCODE -ne 0) { throw "Could not write the package manifest" }

$portable = Join-Path $outputRoot "$stageName-portable.zip"
if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $portable -CompressionLevel Optimal

$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
if ($null -eq $iscc) {
    $commonCompiler = "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    if (Test-Path -LiteralPath $commonCompiler -PathType Leaf) {
        $iscc = Get-Item -LiteralPath $commonCompiler
    }
}
if ($null -ne $iscc) {
    & $iscc.Source `
        "/DVersion=$Version" `
        "/DSourceRoot=$stage" `
        "/DOutputDirectory=$outputRoot" `
        (Join-Path $repositoryRoot "packaging\windows\AgenticDexed.iss")
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
} else {
    Write-Warning "Inno Setup is unavailable; the portable package was created and CI will build the installer."
}

Write-Output "Created $portable"
Write-Output "Staged $stage"
