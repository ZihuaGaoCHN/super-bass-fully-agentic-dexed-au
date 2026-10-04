[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $ReleaseDirectory
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $ReleaseDirectory -PathType Container)) {
    throw "Release directory does not exist: $ReleaseDirectory"
}

$artifacts = Get-ChildItem -LiteralPath $ReleaseDirectory -Recurse -File |
    Where-Object { $_.Extension -in @(".exe", ".vst3") }
if ($artifacts.Count -eq 0) { throw "No signable Windows artifacts were found" }

$signTool = Get-Command signtool.exe -ErrorAction SilentlyContinue
foreach ($artifact in $artifacts) {
    $signature = Get-AuthenticodeSignature -LiteralPath $artifact.FullName
    if ($signature.Status -ne [Management.Automation.SignatureStatus]::Valid) {
        throw "unsigned artifact: $($artifact.FullName) ($($signature.Status))"
    }
    if ($null -ne $signTool) {
        & $signTool.Source verify /pa /all $artifact.FullName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            throw "signature verification failed: $($artifact.FullName)"
        }
    }
}

Write-Output "Verified $($artifacts.Count) signed Windows artifacts"

