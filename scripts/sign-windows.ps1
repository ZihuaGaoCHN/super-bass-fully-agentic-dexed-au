[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $StageRoot,
    [string] $Version = "1.0.1",
    [string] $OutputDirectory,
    [string] $CertificatePath = $env:WINDOWS_CERTIFICATE_PATH,
    [string] $TimestampUrl = "http://timestamp.digicert.com",
    [switch] $DryRun
)

$ErrorActionPreference = "Stop"
$certificatePassword = $env:WINDOWS_CERTIFICATE_PASSWORD
if ([string]::IsNullOrWhiteSpace($CertificatePath) -or
    -not (Test-Path -LiteralPath $CertificatePath -PathType Leaf)) {
    throw "missing signing credential: WINDOWS_CERTIFICATE_PATH"
}
if ([string]::IsNullOrWhiteSpace($certificatePassword)) {
    throw "missing signing credential: WINDOWS_CERTIFICATE_PASSWORD"
}
if (-not (Test-Path -LiteralPath $StageRoot -PathType Container)) {
    throw "Release stage does not exist: $StageRoot"
}

$stage = (Resolve-Path -LiteralPath $StageRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Split-Path -Parent $stage
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
if ($null -eq $signTool) {
    $sdkRoots = Get-ChildItem -LiteralPath "${env:ProgramFiles(x86)}\Windows Kits\10\bin" `
        -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending
    foreach ($sdkRoot in $sdkRoots) {
        $candidate = Join-Path $sdkRoot.FullName "x64\signtool.exe"
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $signTool = Get-Item -LiteralPath $candidate
            break
        }
    }
}
if ($null -eq $signTool) { throw "Windows SignTool is unavailable" }

$binaries = Get-ChildItem -LiteralPath $stage -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".vst3") }
if ($binaries.Count -eq 0) { throw "No Windows binaries were found to sign" }
if ($DryRun) {
    Write-Output "Dry run: $($binaries.Count) staged binaries are ready for signing"
    exit 0
}

foreach ($binary in $binaries) {
    & $signTool.Source sign /fd SHA256 /td SHA256 /tr $TimestampUrl `
        /f $CertificatePath /p $certificatePassword $binary.FullName
    if ($LASTEXITCODE -ne 0) { throw "SignTool failed for $($binary.Name)" }
}

$stageName = Split-Path -Leaf $stage
$portable = Join-Path $outputRoot "$stageName-portable.zip"
if (Test-Path -LiteralPath $portable) { Remove-Item -LiteralPath $portable -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $portable -CompressionLevel Optimal

$iscc = Get-Command iscc.exe -ErrorAction SilentlyContinue
if ($null -eq $iscc) { throw "Inno Setup compiler is unavailable after binary signing" }
& $iscc.Source `
    "/DVersion=$Version" `
    "/DSourceRoot=$stage" `
    "/DOutputDirectory=$outputRoot" `
    (Join-Path $repositoryRoot "packaging\windows\AgenticDexed.iss")
if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed after binary signing" }

$installer = Join-Path $outputRoot "Super-Bass-Fully-Agentic-Dexed-$Version-windows-x64-setup.exe"
if (-not (Test-Path -LiteralPath $installer -PathType Leaf)) {
    throw "Signed-payload installer was not created: $installer"
}
& $signTool.Source sign /fd SHA256 /td SHA256 /tr $TimestampUrl `
    /f $CertificatePath /p $certificatePassword $installer
if ($LASTEXITCODE -ne 0) { throw "SignTool failed for the Windows installer" }

Write-Output "Signed Windows stage and rebuilt final installer/portable package"

