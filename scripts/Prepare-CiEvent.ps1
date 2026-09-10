#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$EventName = $env:GITHUB_EVENT_NAME,
    [string]$EventPath = $env:GITHUB_EVENT_PATH,
    [string]$EventRef = $env:GITHUB_REF,
    [string]$EventSha = $env:GITHUB_SHA,
    [string]$OutputPath = $env:GITHUB_OUTPUT,
    [string]$Repository = $env:GH_REPO,
    [string]$SummaryPath = $env:GITHUB_STEP_SUMMARY
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePolicy.ps1')
. (Join-Path $PSScriptRoot 'ReleaseBuildSupport.ps1')
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
    $decision = Get-ReleaseBuildDecision -Publish $context.Publish -Tag $context.Tag -ExpectedCommit $context.Commit `
        -Repository $Repository -ExpectedReleaseId $(if ($context.ReleaseId) { [long]$context.ReleaseId } else { 0 })
    if ($decision.NeedsBuild -isnot [bool]) { throw 'The release build decision must be a boolean.' }
    $lines = @("publish=$($context.Publish.ToString().ToLowerInvariant())", "tag=$($context.Tag)",
        "commit=$($context.Commit)", "release_id=$($context.ReleaseId)", "needs_build=$($decision.NeedsBuild.ToString().ToLowerInvariant())")
    Add-Content -LiteralPath $OutputPath -Value $lines -Encoding utf8
    Write-Host "Validated event. Publish: $($context.Publish); tag: $($context.Tag); release ID: $($context.ReleaseId)"
    Write-Host "Needs build: $($decision.NeedsBuild). $($decision.Reason)"
    if ($SummaryPath) {
        $summary = @('## Build decision', '', "Event: $EventName; commit: $($context.Commit)", '', [string]$decision.Reason)
        Add-Content -LiteralPath $SummaryPath -Value $summary -Encoding utf8
    }
} catch {
    # Record the actual validation error before finally closes the transcript.
    Write-Host "Event validation failed: $($_.Exception.Message)"
    throw
} finally {
    Stop-Transcript | Out-Null
}
