[CmdletBinding()]
param(
    [string]$EventName = $env:GITHUB_EVENT_NAME,
    [string]$EventPath = $env:GITHUB_EVENT_PATH,
    [string]$EventRef = $env:GITHUB_REF,
    [string]$EventSha = $env:GITHUB_SHA,
    [string]$OutputPath = $env:GITHUB_OUTPUT
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePolicy.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$logDirectory = Join-Path $repoRoot 'target/ci-logs/prepare'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
Start-Transcript -Path (Join-Path $logDirectory 'event.log') -Force | Out-Null
try {
    Write-Host "CI event: $EventName; ref: $EventRef; commit: $EventSha"
    $context = Get-ReleaseEventContext -EventName $EventName -EventPath $EventPath -Ref $EventRef -Sha $EventSha
    if ($context.Publish) {
        & (Join-Path $PSScriptRoot 'Assert-ReleaseTag.ps1') -Tag $context.Tag -ExpectedCommit $context.Commit
    }
    if (-not $OutputPath) { throw 'GITHUB_OUTPUT or -OutputPath is required.' }
    $lines = @("publish=$($context.Publish.ToString().ToLowerInvariant())", "tag=$($context.Tag)",
        "commit=$($context.Commit)", "release_id=$($context.ReleaseId)")
    Add-Content -LiteralPath $OutputPath -Value $lines -Encoding utf8
    Write-Host "Validated event. Publish: $($context.Publish); tag: $($context.Tag); release ID: $($context.ReleaseId)"
} catch {
    # Record the actual validation error before finally closes the transcript.
    Write-Host "Event validation failed: $($_.Exception.Message)"
    throw
} finally {
    Stop-Transcript | Out-Null
}
