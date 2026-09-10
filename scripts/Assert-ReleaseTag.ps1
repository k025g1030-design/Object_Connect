[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [string]$ExpectedCommit = '',
    [string]$SourceDirectory = (Split-Path -Parent $PSScriptRoot)
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePolicy.ps1')
$commit = Get-LocalReleaseTagCommit -SourceDirectory $SourceDirectory -Tag $Tag
if ($ExpectedCommit -and $commit -cne $ExpectedCommit) { throw 'The release tag does not match the event commit.' }
$headCommit = (& git -C $SourceDirectory rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $headCommit -ne $commit) { throw 'Checkout does not match the release tag.' }
& git -C $SourceDirectory merge-base --is-ancestor $commit refs/remotes/origin/master
if ($LASTEXITCODE -ne 0) { throw 'The tagged commit must already belong to origin/master.' }
Write-Host "Validated release tag $Tag at $commit (annotated and lightweight tags are supported)."
