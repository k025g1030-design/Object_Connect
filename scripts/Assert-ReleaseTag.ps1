[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$Tag)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if ($Tag -notmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
    throw 'Release tags must use vMAJOR.MINOR.PATCH, without leading zeroes or prerelease suffixes.'
}
$type = (& git -C $repoRoot cat-file -t "refs/tags/$Tag" | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $type -cne 'tag') { throw 'A release requires an annotated tag (git tag -a).' }
$commit = (& git -C $repoRoot rev-parse "refs/tags/$Tag^{commit}" | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the release tag.' }
$headCommit = (& git -C $repoRoot rev-parse HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $headCommit -ne $commit) { throw 'Checkout does not match the release tag.' }
& git -C $repoRoot merge-base --is-ancestor $commit refs/remotes/origin/master
if ($LASTEXITCODE -ne 0) { throw 'The tagged commit must already belong to origin/master.' }
Write-Host "Validated annotated release tag $Tag at $commit."
