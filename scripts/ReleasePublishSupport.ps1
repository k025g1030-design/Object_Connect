#requires -Version 7.0
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'PackageSupport.ps1')
. (Join-Path $PSScriptRoot 'ReleasePolicy.ps1')

# All remote I/O is behind these small adapters so the publisher can be tested
# without credentials, creating a tag/release, or uploading real assets.
function Invoke-ReleaseGh {
    param([string[]]$Arguments, [string]$OutputFile = '')
    $start = [Diagnostics.ProcessStartInfo]::new((Get-Command gh -ErrorAction Stop).Source)
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.Environment['GH_PROMPT_DISABLED'] = '1'
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        if (-not $process.Start()) { throw 'Could not start GitHub CLI.' }
        $errorTask = $process.StandardError.ReadToEndAsync()
        $output = ''
        if ($OutputFile) {
            $stream = [IO.File]::Open($OutputFile, [IO.FileMode]::CreateNew)
            try { $process.StandardOutput.BaseStream.CopyTo($stream) } finally { $stream.Dispose() }
        } else {
            $output = $process.StandardOutput.ReadToEnd()
        }
        $process.WaitForExit()
        return @{ ExitCode = $process.ExitCode; Output = $output; ErrorOutput = $errorTask.GetAwaiter().GetResult() }
    } finally { $process.Dispose() }
}

function ConvertFrom-ReleaseApiResponse {
    param([hashtable]$Response, [switch]$AllowNotFound)
    $match = [regex]::Match($Response.Output, '\AHTTP/\S+\s+(?<status>[0-9]{3})[^\r\n]*\r?\n(?:[^\r\n]+\r?\n)*\r?\n(?<body>[\s\S]*)\z')
    if (-not $match.Success) { throw "GitHub API returned no verifiable HTTP response (exit $($Response.ExitCode)): $($Response.ErrorOutput)" }
    $status = [int]$match.Groups['status'].Value
    if ($status -eq 404 -and $AllowNotFound) { return $null }
    if ($Response.ExitCode -ne 0 -or $status -lt 200 -or $status -ge 300) {
        throw "GitHub API failed (HTTP $status, exit $($Response.ExitCode)): $($Response.ErrorOutput)"
    }
    return ConvertFrom-Json -InputObject $match.Groups['body'].Value -AsHashtable -NoEnumerate -ErrorAction Stop
}

function Get-ReleaseApiObject {
    param([string]$Endpoint, [switch]$AllowNotFound)
    $response = Invoke-ReleaseGh -Arguments @('api', $Endpoint, '--include', '--header', 'Accept: application/vnd.github+json', '--header', 'X-GitHub-Api-Version: 2022-11-28')
    return ConvertFrom-ReleaseApiResponse $response -AllowNotFound:$AllowNotFound
}

function Get-ReleaseAssets {
    param([string]$Repository, [long]$ReleaseId)
    $assets = [Collections.Generic.List[object]]::new()
    for ($page = 1; ; $page++) {
        $batch = Get-ReleaseApiObject "repos/$Repository/releases/$ReleaseId/assets?per_page=100&page=$page"
        if ($batch -isnot [array]) { throw 'GitHub returned an invalid release asset list.' }
        foreach ($asset in $batch) { $assets.Add($asset) }
        if ($batch.Count -lt 100) { break }
    }
    return ,$assets.ToArray()
}

function Save-ReleaseAsset {
    param([string]$Repository, [long]$AssetId, [string]$Destination)
    $response = Invoke-ReleaseGh -Arguments @('api', "repos/$Repository/releases/assets/$AssetId", '--header', 'Accept: application/octet-stream', '--header', 'X-GitHub-Api-Version: 2022-11-28') -OutputFile $Destination
    if ($response.ExitCode -ne 0) { throw "Downloading release asset $AssetId failed: $($response.ErrorOutput)" }
}

function New-ReleaseRecord {
    param([string]$Repository, [string]$Tag)
    # --verify-tag prevents gh from manufacturing a missing tag. No edit/delete
    # command exists in this publisher, and existing release prose is untouched.
    $response = Invoke-ReleaseGh -Arguments @('release', 'create', $Tag, '--repo', $Repository, '--verify-tag', '--title', $Tag, '--generate-notes')
    if ($response.ExitCode -ne 0) { throw "Creating release failed; no automatic mutation retry: $($response.ErrorOutput)" }
}

function Add-ReleaseAsset {
    param([string]$Repository, [long]$ReleaseId, [string]$Path)
    $name = [Uri]::EscapeDataString([IO.Path]::GetFileName($Path))
    # Address the stable release ID, not a name that could be deleted/recreated.
    # GitHub rejects duplicate names (422); never use --clobber or DELETE.
    $response = Invoke-ReleaseGh -Arguments @('api', "https://uploads.github.com/repos/$Repository/releases/$ReleaseId/assets?name=$name", '--method', 'POST', '--include', '--input', $Path, '--header', 'Content-Type: application/octet-stream', '--header', 'X-GitHub-Api-Version: 2022-11-28')
    $null = ConvertFrom-ReleaseApiResponse $response
}

function Assert-RemoteReleaseTag {
    param([string]$Repository, [string]$Tag, [string]$ExpectedCommit)
    $reference = Get-ReleaseApiObject "repos/$Repository/git/ref/tags/$Tag"
    if ($reference.ref -cne "refs/tags/$Tag") { throw 'Remote tag reference does not match the requested version.' }
    $target = $reference.object
    if ($target.type -ceq 'tag') {
        $tagObject = Get-ReleaseApiObject "repos/$Repository/git/tags/$($target.sha)"
        $target = $tagObject.object
    }
    if ($target.type -cne 'commit' -or $target.sha -cne $ExpectedCommit) {
        throw 'Remote tag moved or does not identify the tested commit (nested tags are unsupported).'
    }
}

function Assert-PublishableRelease {
    param([System.Collections.IDictionary]$Release, [string]$Tag, [long]$ExpectedReleaseId = 0)
    if (-not $Release -or $Release.tag_name -cne $Tag -or [long]$Release.id -le 0) { throw 'Release identity does not match the requested version.' }
    if ($ExpectedReleaseId -gt 0 -and [long]$Release.id -ne $ExpectedReleaseId) { throw 'Release ID changed since the triggering event; refusing a replacement release.' }
    if ($Release.draft -isnot [bool] -or $Release.prerelease -isnot [bool] -or $Release.draft -or $Release.prerelease -or -not $Release.published_at) {
        throw 'Only an already published regular release may receive assets; drafts and prereleases are not changed.'
    }
}

function Get-ReleaseFileHash {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ReleaseStreamHash {
    param([IO.Stream]$Stream)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [Convert]::ToHexString($hash.ComputeHash($Stream)).ToLowerInvariant() } finally { $hash.Dispose() }
}

function Assert-PackageMemberName {
    param([string]$Name)
    if (-not $Name -or $Name.StartsWith('/') -or $Name -match '[\\:\x00-\x1f]' -or $Name -match '(^|/)(\.|\.\.)(/|$)' -or $Name.Contains('//')) {
        throw "Unsafe package member path: $Name"
    }
}

function Assert-OfficialReleaseBundle {
    param([string]$Directory, [string]$Tag, [string]$ExpectedCommit)
    $baseName = "BloodLine-windows-x64-$Tag"
    $names = @("$baseName.zip", "$baseName.zip.sha256", "$baseName.build-info.json")
    $paths = [ordered]@{}
    $hashes = [ordered]@{}
    foreach ($name in $names) {
        $path = Join-Path $Directory $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required managed release asset is missing: $name" }
        $paths[$name] = (Resolve-Path -LiteralPath $path).ProviderPath
        $hashes[$name] = Get-ReleaseFileHash $path
    }
    $checksum = [IO.File]::ReadAllText($paths[$names[1]])
    $checksumPattern = '\A([a-fA-F0-9]{64})  ' + [regex]::Escape($names[0]) + '\r?\n?\z'
    if ($checksum -notmatch $checksumPattern -or $Matches[1].ToLowerInvariant() -cne $hashes[$names[0]]) { throw 'Release ZIP checksum or checksum filename is invalid.' }
    $metadata = ConvertFrom-Json ([IO.File]::ReadAllText($paths[$names[2]])) -AsHashtable -ErrorAction Stop
    if ($metadata.schemaVersion -ne 1 -or $metadata.version -cne $Tag -or
        $metadata.built.schemaVersion -ne 1 -or $metadata.built.configuration -cne 'Release' -or $metadata.built.architecture -cne 'x64' -or
        $metadata.built.commit -cne $ExpectedCommit -or $metadata.resourceSourceCommit -cne $ExpectedCommit -or
        $metadata.built.sourceDirty -isnot [bool] -or $metadata.built.sourceDirty -or
        $metadata.resourceSourceDirty -isnot [bool] -or $metadata.resourceSourceDirty) {
        throw 'Release metadata must identify this exact version and tested clean Release x64 commit.'
    }
    $null = Assert-ReleaseDependencies ($metadata.directDllDependencies -join "`n")
    $expected = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $expected.Add('Object_Connect.exe', $metadata.built.executableSha256)
    $expected.Add('build-info.json', $hashes[$names[2]])
    foreach ($section in @(@{ Field = 'resources'; Prefix = 'Resources/' }, @{ Field = 'licenses'; Prefix = 'LICENSES/KamataEngine/' }, @{ Field = 'projectLicenses'; Prefix = 'LICENSES/' })) {
        $manifest = $metadata[$section.Field]
        if ($manifest -isnot [System.Collections.IDictionary] -or $manifest.Count -eq 0) { throw "Missing package manifest: $($section.Field)" }
        foreach ($member in $manifest.Keys) {
            Assert-PackageMemberName $member
            $expected.Add($section.Prefix + $member, [string]$manifest[$member])
        }
    }
    foreach ($required in @('Resources/fonts/BIZUDPGothic-Regular.ttf', 'Resources/data/levels.csv', 'Resources/data/nodes.csv', 'LICENSES/BIZUDPGothic-OFL.txt', 'LICENSES/KamataEngine/ImGui.txt')) {
        if (-not $expected.ContainsKey($required)) { throw "Required packaged resource or notice is missing: $required" }
    }
    $actual = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [IO.Compression.ZipFile]::OpenRead($paths[$names[0]])
    try {
        foreach ($entry in $zip.Entries) {
            Assert-PackageMemberName $entry.FullName
            if ($entry.FullName.EndsWith('/')) { continue }
            if (-not $actual.Add($entry.FullName) -or -not $expected.ContainsKey($entry.FullName)) { throw "Duplicate or unexpected ZIP member: $($entry.FullName)" }
            if ($entry.FullName -match '\.(dll|pdb|lib)$' -or ($entry.FullName -match '\.exe$' -and $entry.FullName -cne 'Object_Connect.exe')) { throw "Development binary is forbidden in the player ZIP: $($entry.FullName)" }
            $expectedHash = $expected[$entry.FullName]
            if ($expectedHash -notmatch '^[a-fA-F0-9]{64}$') { throw "Invalid content hash for ZIP member: $($entry.FullName)" }
            $stream = $entry.Open()
            try { $actualHash = Get-ReleaseStreamHash $stream } finally { $stream.Dispose() }
            if ($actualHash -ine $expectedHash) { throw "ZIP content does not match metadata: $($entry.FullName)" }
        }
        if ($actual.Count -ne $expected.Count) { throw 'ZIP is missing files declared in the package manifests.' }
    } finally { $zip.Dispose() }
    return @{ Names = $names; Paths = $paths; Hashes = $hashes }
}

function Get-ManagedReleaseAssets {
    param([object[]]$Assets, [string[]]$Names)
    $managed = @{}
    foreach ($asset in $Assets) {
        if ($Names -icontains $asset.name) {
            if ($Names -cnotcontains $asset.name -or $managed.ContainsKey($asset.name) -or $asset.state -cne 'uploaded') {
                throw "Conflicting duplicate, case, or incomplete managed asset: $($asset.name)"
            }
            $managed[$asset.name] = $asset
        }
    }
    return $managed
}

function Invoke-ReleasePublish {
    param([string]$Tag, [string]$ExpectedCommit, [string]$PackageDirectory, [string]$Repository, [long]$ExpectedReleaseId = 0)
    Assert-OfficialReleaseVersion $Tag
    if ($ExpectedCommit -cnotmatch '^[a-f0-9]{40}$' -or
        $Repository -notmatch '^[a-zA-Z0-9_.-]+/[a-zA-Z0-9_.-]+$' -or $ExpectedReleaseId -lt 0) { throw 'Invalid release version, tested commit, repository, or event release ID.' }
    # Validate every local byte before any remote mutation, even on a no-op rerun.
    $bundle = Assert-OfficialReleaseBundle $PackageDirectory $Tag $ExpectedCommit
    Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
    $endpoint = "repos/$Repository/releases/tags/$Tag"
    $release = Get-ReleaseApiObject $endpoint -AllowNotFound
    if (-not $release -and $ExpectedReleaseId -gt 0) { throw 'The release from the triggering event no longer exists; it will not be recreated.' }
    $managed = @{}
    if ($release) {
        Assert-PublishableRelease $release $Tag $ExpectedReleaseId
        $managed = Get-ManagedReleaseAssets (Get-ReleaseAssets $Repository $release.id) $bundle.Names
    }
    $temporaryRoot = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $temporary = Join-Path $temporaryRoot ('bloodline-release-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $temporary | Out-Null
    try {
        foreach ($name in $managed.Keys) { Save-ReleaseAsset $Repository $managed[$name].id (Join-Path $temporary $name) }
        if ($managed.Count -eq 3) {
            # A prior build can have different timestamps/ZIP bytes. A complete,
            # internally verified release for the same clean commit is immutable.
            $null = Assert-OfficialReleaseBundle $temporary $Tag $ExpectedCommit
            Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
            Assert-PublishableRelease (Get-ReleaseApiObject $endpoint) $Tag $release.id
            return @{ Status = 'already complete (verified, unchanged)'; UploadedCount = 0 }
        }
        foreach ($name in $managed.Keys) {
            if ((Get-ReleaseFileHash (Join-Path $temporary $name)) -cne $bundle.Hashes[$name]) {
                throw "Partial release conflicts with this build: $name. Existing assets were not changed; use the original artifact or publish a new version."
            }
        }
        Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
        if (-not $release) {
            New-ReleaseRecord $Repository $Tag
            $release = Get-ReleaseApiObject $endpoint
            Assert-PublishableRelease $release $Tag
        }
        $uploadedCount = 0
        foreach ($name in $bundle.Names) {
            if ($managed.ContainsKey($name)) { continue }
            Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
            Assert-PublishableRelease (Get-ReleaseApiObject $endpoint) $Tag $release.id
            Add-ReleaseAsset $Repository $release.id $bundle.Paths[$name]
            $uploadedCount++
        }
        # Confirm the actual remote set, including uploaded bytes. Failures never
        # delete/overwrite anything; a rerun can inspect and resume partial state.
        Assert-RemoteReleaseTag $Repository $Tag $ExpectedCommit
        Assert-PublishableRelease (Get-ReleaseApiObject $endpoint) $Tag $release.id
        $finalManaged = Get-ManagedReleaseAssets (Get-ReleaseAssets $Repository $release.id) $bundle.Names
        if ($finalManaged.Count -ne 3) { throw 'Release upload did not produce all three managed assets.' }
        $verified = Join-Path $temporary 'verified'
        New-Item -ItemType Directory -Path $verified | Out-Null
        foreach ($name in $bundle.Names) { Save-ReleaseAsset $Repository $finalManaged[$name].id (Join-Path $verified $name) }
        $null = Assert-OfficialReleaseBundle $verified $Tag $ExpectedCommit
        return @{ Status = 'published / missing assets added'; UploadedCount = $uploadedCount }
    } finally {
        $resolved = [IO.Path]::GetFullPath($temporary)
        if ($resolved.StartsWith($temporaryRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^bloodline-release-[a-f0-9]{32}$') {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
