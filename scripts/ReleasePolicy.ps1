Set-StrictMode -Version Latest

function Assert-OfficialReleaseVersion {
    param([Parameter(Mandatory = $true)][string]$Version)
    if ($Version.Length -gt 80 -or $Version -cnotmatch '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$') {
        throw 'Official versions must use vMAJOR.MINOR.PATCH (lowercase v, no leading zeroes or prerelease suffixes, at most 80 characters).'
    }
}

function Get-LocalReleaseTagCommit {
    param([Parameter(Mandatory = $true)][string]$SourceDirectory,
        [Parameter(Mandatory = $true)][string]$Tag)
    Assert-OfficialReleaseVersion $Tag
    $tagRef = "refs/tags/$Tag"
    # show-ref --quiet avoids native stderr on a missing tag in Windows PowerShell.
    & git -C $SourceDirectory show-ref --verify --quiet $tagRef
    if ($LASTEXITCODE -ne 0) { throw "Release tag does not exist: $Tag" }
    $type = (& git -C $SourceDirectory cat-file -t $tagRef | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $type -cnotin @('tag', 'commit')) {
        throw 'A release tag must point to a commit (lightweight or annotated).'
    }
    if ($type -ceq 'tag') {
        $tagObject = @(& git -C $SourceDirectory cat-file -p $tagRef)
        if ($LASTEXITCODE -ne 0 -or $tagObject.Count -lt 2 -or $tagObject[1] -cne 'type commit') {
            throw 'An annotated release tag must point directly to a commit, not another tag or object.'
        }
    }
    $commit = (& git -C $SourceDirectory rev-parse "$tagRef^{commit}" | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -cnotmatch '^[a-f0-9]{40}$') { throw 'Cannot resolve the release tag commit.' }
    return $commit
}

function Get-ReleaseEventContext {
    param([Parameter(Mandatory = $true)][string]$EventName,
        [Parameter(Mandatory = $true)][string]$EventPath,
        [Parameter(Mandatory = $true)][string]$Ref,
        [Parameter(Mandatory = $true)][string]$Sha)
    if ($Sha -cnotmatch '^[a-f0-9]{40}$') { throw 'The event commit must be a full Git commit SHA.' }
    $eventData = Get-Content -LiteralPath $EventPath -Raw | ConvertFrom-Json
    $context = [pscustomobject]@{ Publish = $false; Tag = ''; Commit = $Sha; ReleaseId = '' }
    if ($EventName -ceq 'release') {
        if ($eventData.action -cne 'published' -or $null -eq $eventData.release) {
            throw 'Only release.published is a supported release event.'
        }
        $release = $eventData.release
        if ($release.draft -isnot [bool] -or $release.prerelease -isnot [bool] -or $release.draft -or $release.prerelease) {
            throw 'Only published stable releases are supported; drafts and prereleases are not published by CI.'
        }
        Assert-OfficialReleaseVersion $release.tag_name
        if ($Ref -cne "refs/tags/$($release.tag_name)") { throw 'Release tag and event ref do not match.' }
        if ([string]$release.id -notmatch '^[1-9][0-9]*$') { throw 'The release event must identify an existing Release ID.' }
        $context.Publish = $true
        $context.Tag = $release.tag_name
        $context.ReleaseId = [string]$release.id
    } elseif ($EventName -ceq 'push') {
        if ($eventData.PSObject.Properties['deleted'] -and $eventData.deleted) { throw 'Deleted refs cannot be built or published.' }
        if ($Ref.StartsWith('refs/tags/', [StringComparison]::Ordinal)) {
            $context.Tag = $Ref.Substring('refs/tags/'.Length)
            Assert-OfficialReleaseVersion $context.Tag
            $context.Publish = $true
        } elseif ($Ref -cne 'refs/heads/master') {
            throw 'Branch push builds are limited to master.'
        }
    } elseif ($EventName -cnotin @('pull_request', 'workflow_dispatch')) {
        throw "Unsupported CI event: $EventName"
    }
    # GITHUB_SHA is the tagged commit for release events. target_commitish may be
    # a moving branch, so it is deliberately never used for checkout or provenance.
    return $context
}
