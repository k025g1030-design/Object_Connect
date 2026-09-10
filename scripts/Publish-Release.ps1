#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [Parameter(Mandatory = $true)][string]$ExpectedCommit,
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [long]$ExpectedReleaseId = 0
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePublishSupport.ps1')
if (-not $env:GH_TOKEN) { throw 'GH_TOKEN is required for publishing.' }
if (-not $env:GH_REPO) { throw 'GH_REPO must identify owner/repository.' }
$result = Invoke-ReleasePublish -Tag $Tag -ExpectedCommit $ExpectedCommit -PackageDirectory $PackageDirectory `
    -Repository $env:GH_REPO -ExpectedReleaseId $ExpectedReleaseId
Write-Host "Release ${Tag}: $($result.Status); uploaded $($result.UploadedCount) managed assets."
