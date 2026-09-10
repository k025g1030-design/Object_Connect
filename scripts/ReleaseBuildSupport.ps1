#requires -Version 7.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'ReleasePublishSupport.ps1')

function Test-SuccessfulReleaseBuild {
    param([string]$Repository, [string]$Tag, [string]$ExpectedCommit)
    for ($page = 1; ; $page++) {
        $response = Get-ReleaseApiObject "repos/$Repository/actions/workflows/windows-build.yml/runs?head_sha=$ExpectedCommit&branch=$Tag&status=success&per_page=100&page=$page"
        if ($response -isnot [System.Collections.IDictionary] -or $response.workflow_runs -isnot [array]) {
            throw 'GitHub returned an invalid release workflow history.'
        }
        foreach ($run in $response.workflow_runs) {
            if ($run.head_sha -ceq $ExpectedCommit -and $run.head_branch -ceq $Tag -and
                $run.event -cin @('push', 'release') -and $run.status -ceq 'completed' -and $run.conclusion -ceq 'success') {
                return $true
            }
        }
        if ($response.workflow_runs.Count -lt 100) { return $false }
    }
}

function Get-ReleaseBuildAssetSnapshot {
    param([object[]]$Assets, [string[]]$Names)
    $managed = Get-ManagedReleaseAssets $Assets $Names
    $identities = @{}
    $assetIds = [Collections.Generic.HashSet[long]]::new()
    foreach ($name in $managed.Keys) {
        $asset = $managed[$name]
        $id = 0L
        $size = 0L
        $updatedAt = [DateTimeOffset]::MinValue
        if ($asset -isnot [System.Collections.IDictionary] -or
            -not $asset.Contains('id') -or -not [long]::TryParse([string]$asset.id, [ref]$id) -or $id -le 0 -or
            -not $assetIds.Add($id) -or
            -not $asset.Contains('size') -or -not [long]::TryParse([string]$asset.size, [ref]$size) -or $size -le 0 -or
            -not $asset.Contains('updated_at') -or -not [DateTimeOffset]::TryParse([string]$asset.updated_at, [ref]$updatedAt)) {
            throw "Malformed managed release asset identity: $name"
        }
        $digest = ''
        if ($asset.Contains('digest') -and $null -ne $asset.digest) {
            $digest = [string]$asset.digest
            if ($digest -cnotmatch '^sha256:[a-f0-9]{64}$') { throw "Malformed managed release asset digest: $name" }
        }
        # Copy primitive values: a refreshed API response must not silently
        # replace the identity of any bytes just downloaded and verified.
        $identities[$name] = "$id|$size|$($asset.updated_at)|$digest"
    }
    return @{ Managed = $managed; Identities = $identities }
}

function Get-ReleaseBuildDecision {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][bool]$Publish,
        [string]$Tag = '',
        [string]$ExpectedCommit = '',
        [string]$Repository = '',
        [long]$ExpectedReleaseId = 0
    )
    if (-not $Publish) {
        return @{ NeedsBuild = $true; Reason = 'Non-release event: run the normal build and tests.' }
    }
    Assert-OfficialReleaseVersion $Tag
    if ($ExpectedCommit -cnotmatch '^[a-f0-9]{40}$' -or
        $Repository -notmatch '^[a-zA-Z0-9_.-]+/[a-zA-Z0-9_.-]+$' -or $ExpectedReleaseId -lt 0) {
        throw 'Invalid release version, tested commit, repository, or event release ID.'
    }
    Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
    $endpoint = "repos/$Repository/releases/tags/$Tag"
    $release = Get-ReleaseApiObject $endpoint -AllowNotFound
    if (-not $release) {
        if ($ExpectedReleaseId -gt 0) { throw 'The release from the triggering event no longer exists; it will not be recreated.' }
        return @{ NeedsBuild = $true; Reason = 'No published release exists yet: build, test and package this tag.' }
    }
    Assert-PublishableRelease $release $Tag $ExpectedReleaseId
    $releaseId = [long]$release.id
    $baseName = "BloodLine-windows-x64-$Tag"
    $names = @("$baseName.zip", "$baseName.zip.sha256", "$baseName.build-info.json")
    $snapshot = Get-ReleaseBuildAssetSnapshot (Get-ReleaseAssets $Repository $releaseId) $names
    if ($snapshot.Managed.Count -ne $names.Count) {
        return @{ NeedsBuild = $true; Reason = 'Release assets are missing or partial: a complete successful package has not been established.' }
    }

    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $temporary = Join-Path $temporaryRoot ('bloodline-release-build-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    try {
        foreach ($name in $names) {
            $asset = $snapshot.Managed[$name]
            $destination = Join-Path $temporary $name
            Save-ReleaseAsset $Repository $asset.id $destination
            if ((Get-Item -LiteralPath $destination).Length -ne [long]$asset.size) { throw "Downloaded release asset size changed: $name" }
            if ($asset.Contains('digest') -and $null -ne $asset.digest -and
                ('sha256:' + (Get-ReleaseFileHash $destination)) -cne $asset.digest) {
                throw "Downloaded release asset digest changed: $name"
            }
        }
        # Release existence or a green workflow alone is not a deduplication
        # marker. Verify the entire immutable bundle for this clean commit.
        $null = Assert-OfficialReleaseBundle $temporary $Tag $ExpectedCommit
        if (-not (Test-SuccessfulReleaseBuild $Repository $Tag $ExpectedCommit)) {
            return @{ NeedsBuild = $true; Reason = 'Published bytes are valid, but no successful release workflow for this tag and commit is recorded: run all build and test gates.' }
        }
        Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
        Assert-PublishableRelease (Get-ReleaseApiObject $endpoint) $Tag $releaseId
        $current = Get-ReleaseBuildAssetSnapshot (Get-ReleaseAssets $Repository $releaseId) $names
        if ($current.Managed.Count -ne $names.Count) { throw 'Managed release assets changed while the completed package was being verified.' }
        foreach ($name in $names) {
            if ($current.Identities[$name] -cne $snapshot.Identities[$name]) {
                throw "Managed release asset changed while the completed package was being verified: $name"
            }
        }
        return @{ NeedsBuild = $false; Reason = 'The complete published package for this exact clean commit was verified; skip duplicate build, packaging and publication.' }
    } finally {
        $resolved = [IO.Path]::GetFullPath($temporary)
        if ($resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and
            [IO.Path]::GetFileName($resolved) -match '^bloodline-release-build-[a-f0-9]{32}$') {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
