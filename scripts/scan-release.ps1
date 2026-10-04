[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ReleaseDirectory,
    [string] $Version = "1.0.1"
)

$ErrorActionPreference = "Stop"
$releaseRoot = [IO.Path]::GetFullPath($ReleaseDirectory)
if (-not (Test-Path -LiteralPath $releaseRoot -PathType Container)) {
    throw "Release directory does not exist: $releaseRoot"
}
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$temporaryRoot = Join-Path ([IO.Path]::GetTempPath()) ("agentic-dexed-scan-" + [Guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
$issues = [Collections.Generic.List[string]]::new()
$filesScanned = 0

$canary = "sk-agentic-release-" + "canary-0123456789"
$authorizationPattern = "Authoriza" + "tion:\s*Bearer\s+[A-Za-z0-9._-]+"
$privateKeyPattern = "-----BEGIN\s+(?:RSA\s+|EC\s+|OPENSSH\s+)?PRIVATE\s+KEY-----"
$workspacePattern = "[A-Za-z]:[\\/][^\r\n]{0,300}[\\/]\\.orca[\\/]workspaces|[A-Za-z]:[\\/]a[\\/][^\\/\r\n]+[\\/][^\\/\r\n]+|/(?:home|Users)/runner/work/"
$payloadPattern = 'unredacted_(?:request|response)\s*[:=]|data:audio/(?:wav|mpeg)|"(?:pcm_samples|audio_bytes)"\s*:'
$contentPatterns = @($canary, $authorizationPattern, $privateKeyPattern, $workspacePattern, $payloadPattern)

function Add-Issue([string] $Message) { $script:issues.Add($Message) }

function Test-IsStagingPath([string] $Path) {
    return $Path -match '[\\/]Agentic-Dexed-[^\\/]+-(?:source|windows-x64|macos-universal)[\\/]'
}

function Test-Tree([string] $Root, [bool] $SkipStaging = $false) {
    $rootPath = [IO.Path]::GetFullPath($Root)
    foreach ($file in Get-ChildItem -LiteralPath $rootPath -Recurse -File -Force) {
        if ($SkipStaging -and $file.FullName -match '[\\/]Agentic-Dexed-[^\\/]+-(?:source|windows-x64|macos-universal)[\\/]') {
            continue
        }
        $script:filesScanned++
        $relative = [IO.Path]::GetRelativePath($rootPath, $file.FullName).Replace('\', '/')
        if ($file.Extension -match '^\.(pfx|p12|pem|key)$') {
            Add-Issue "signing credential file: $relative"
        }
        if ($file.Extension -match '^\.(exe|dll|dylib|vst3)$') {
            $sourceFile = $relative -match '(^|/)Agentic-Dexed-[^/]+-source/'
            $allowedBinary = if ($sourceFile) {
                $relative -match '(^|/)Agentic-Dexed-[^/]+-source/libs/MTS-ESP/libMTS/'
            } else {
                $file.Name -eq "Agentic Dexed.exe" -or
                $file.Name -eq "Agentic Dexed.vst3" -or
                $file.Name -match '^Agentic-Dexed-[0-9.]+-windows-x64-setup\.exe$'
            }
            if (-not $allowedBinary) { Add-Issue "unexpected executable: $relative" }
        }
        if ($file.Length -le 64MB) {
            $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName))
            foreach ($pattern in $contentPatterns) {
                if ($text -match $pattern) {
                    Add-Issue "sensitive content in $relative"
                    break
                }
            }
        }
    }
    foreach ($link in Get-ChildItem -LiteralPath $rootPath -Recurse -Force |
            Where-Object { $_.Attributes -band [IO.FileAttributes]::ReparsePoint }) {
        try { $target = [IO.Path]::GetFullPath($link.Target) } catch { $target = "" }
        $prefix = $rootPath.TrimEnd('\') + '\'
        if ([string]::IsNullOrWhiteSpace($target) -or
            -not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
            Add-Issue "symlink escapes package root: $($link.FullName)"
        }
    }
}

try {
    Test-Tree $releaseRoot $true
    $archiveIndex = 0
    foreach ($archive in Get-ChildItem -LiteralPath $releaseRoot -Recurse -File |
            Where-Object {
                $_.Name -match '\.zip$|\.tar\.gz$' -and
                -not (Test-IsStagingPath $_.FullName)
            }) {
        $archiveIndex++
        $destination = Join-Path $temporaryRoot "archive-$archiveIndex"
        New-Item -ItemType Directory -Force -Path $destination | Out-Null
        & tar.exe -xf $archive.FullName -C $destination
        if ($LASTEXITCODE -ne 0) { throw "Could not extract $($archive.Name)" }
        Test-Tree $destination
    }

    if ($issues.Count -gt 0) {
        $issues | ForEach-Object { [Console]::Error.WriteLine($_) }
        throw "release scan failed with $($issues.Count) issue(s)"
    }

    $packages = Get-ChildItem -LiteralPath $releaseRoot -Recurse -File |
        Where-Object {
            -not (Test-IsStagingPath $_.FullName) -and
            ($_.Name -match '\.zip$|\.tar\.gz$|\.pkg$|\.dmg$' -or
             $_.Name -match '-setup\.exe$')
        } | Sort-Object FullName
    $checksumLines = foreach ($package in $packages) {
        $relative = [IO.Path]::GetRelativePath($releaseRoot, $package.FullName).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $package.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
    Set-Content -LiteralPath (Join-Path $releaseRoot "SHA256SUMS") -Value $checksumLines -Encoding utf8

    $submodules = @()
    foreach ($line in (& git -C $repositoryRoot submodule status --recursive)) {
        if ($line -match '^[ +U-]?([0-9a-f]{40})\s+([^\s]+)') {
            $submodules += [ordered]@{ path = $Matches[2]; commit = $Matches[1] }
        }
    }
    $logs = Get-ChildItem -LiteralPath $releaseRoot -Recurse -File -Filter "*.log" |
        ForEach-Object { [IO.Path]::GetRelativePath($releaseRoot, $_.FullName).Replace('\', '/') }
    $sourceArchive = $packages | Where-Object Name -match '-source\.zip$' | Select-Object -First 1
    $releaseManifest = [ordered]@{
        product = "Agentic Dexed"
        version = $Version
        git_commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
        submodules = $submodules
        platforms = @(
            [ordered]@{ name = "windows"; architectures = @("x86_64") },
            [ordered]@{ name = "macos"; architectures = @("x86_64", "arm64") }
        )
        bundle_id = "com.agenticdexed.AgenticDexed"
        plugin_code = "AgDx"
        validation_logs = @($logs)
        source_archive = if ($null -eq $sourceArchive) { $null } else { $sourceArchive.Name }
        artifacts = @($packages | ForEach-Object { $_.Name })
    }
    $releaseManifest | ConvertTo-Json -Depth 6 | Set-Content `
        -LiteralPath (Join-Path $releaseRoot "release-manifest.json") -Encoding utf8
    [ordered]@{ result = "pass"; files_scanned = $filesScanned; archives_scanned = $archiveIndex } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $releaseRoot "release-scan-report.json") -Encoding utf8
    Write-Output "Release scan passed: $filesScanned files across $archiveIndex archives"
} finally {
    if (Test-Path -LiteralPath $temporaryRoot) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
