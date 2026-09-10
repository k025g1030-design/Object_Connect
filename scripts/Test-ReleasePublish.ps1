#requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePublishSupport.ps1')
$testRoot = Join-Path (Split-Path -Parent $PSScriptRoot) ('target/release-publish-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$tag = 'v1.2.3'
$commit = 'a' * 40
$repository = 'test-owner/test-repo'
$script:assertionCount = 0

function Assert-Test {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) { throw "Assertion failed: $Message" }
    $script:assertionCount++
}

function Assert-Rejected {
    param([scriptblock]$Action, [string]$Pattern)
    $message = ''
    try { $null = & $Action } catch { $message = $_.Exception.Message }
    Assert-Test ($message -like $Pattern) "Expected '$Pattern', got '$message'"
}

function New-BundleFixture {
    param([string]$Label, [string]$Commit = $commit, [bool]$Dirty = $false)
    $directory = Join-Path $testRoot $Label
    $staging = Join-Path $directory 'staging'
    foreach ($folder in @('Resources/fonts', 'Resources/data', 'LICENSES/KamataEngine')) {
        New-Item -ItemType Directory -Path (Join-Path $staging $folder) -Force | Out-Null
    }
    foreach ($file in @('Object_Connect.exe', 'Resources/fonts/BIZUDPGothic-Regular.ttf', 'Resources/data/levels.csv', 'Resources/data/nodes.csv', 'LICENSES/BIZUDPGothic-OFL.txt', 'LICENSES/KamataEngine/ImGui.txt')) {
        [IO.File]::WriteAllText((Join-Path $staging $file), "fixture $file", [Text.UTF8Encoding]::new($false))
    }
    $metadata = [ordered]@{
        schemaVersion = 1; version = $tag; packagedAtUtc = $Label
        built = [ordered]@{
            schemaVersion = 1; configuration = 'Release'; architecture = 'x64'
            commit = $Commit; sourceDirty = $Dirty
            executableSha256 = Get-ReleaseFileHash (Join-Path $staging 'Object_Connect.exe')
        }
        resourceSourceCommit = $Commit; resourceSourceDirty = $Dirty
        resources = Get-ResourceManifest (Join-Path $staging 'Resources')
        licenses = Get-ResourceManifest (Join-Path $staging 'LICENSES/KamataEngine')
        projectLicenses = @{ 'BIZUDPGothic-OFL.txt' = Get-ReleaseFileHash (Join-Path $staging 'LICENSES/BIZUDPGothic-OFL.txt') }
        directDllDependencies = @('kernel32.dll', 'd3dcompiler_47.dll')
    }
    $baseName = "BloodLine-windows-x64-$tag"
    $json = ($metadata | ConvertTo-Json -Depth 12) + "`n"
    [IO.File]::WriteAllText((Join-Path $staging 'build-info.json'), $json, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $directory "$baseName.build-info.json"), $json, [Text.UTF8Encoding]::new($false))
    [IO.Compression.ZipFile]::CreateFromDirectory($staging, (Join-Path $directory "$baseName.zip"))
    [IO.File]::WriteAllText((Join-Path $directory "$baseName.zip.sha256"), "$(Get-ReleaseFileHash (Join-Path $directory "$baseName.zip"))  $baseName.zip`n")
    return $directory
}

function Reset-RemoteFixture {
    param([switch]$Missing)
    $script:remote = @{
        Release = @{ id = 123; tag_name = $tag; draft = $false; prerelease = $false; published_at = '2026-01-01'; name = 'Human title'; body = 'Human release notes' }
        Assets = [Collections.Generic.List[object]]::new(); Files = @{}; NextId = 1000
        CreateCount = 0; UploadCount = 0; ApiCount = 0; TagType = 'commit'; TagCommit = $commit
        Nested = $false; ReadFailure = ''; UploadFailureAt = 0; DownloadFailure = $false
    }
    if ($Missing) { $script:remote.Release = $null }
}

function Add-FixtureAsset {
    param([string]$Name, [string]$Path)
    $script:remote.NextId++
    $script:remote.Assets.Add(@{ id = $script:remote.NextId; name = $Name; state = 'uploaded' })
    $script:remote.Files[$script:remote.NextId] = $Path
}

# The tests intentionally replace all five network/mutation adapters. No gh
# binary or token is needed; accidental use of the low-level adapter fails.
function Invoke-ReleaseGh { throw 'Unexpected real GitHub CLI access in offline publisher test.' }
function Get-ReleaseApiObject {
    param([string]$Endpoint, [switch]$AllowNotFound)
    $script:remote.ApiCount++
    if ($script:remote.ReadFailure) { throw $script:remote.ReadFailure }
    if ($Endpoint -ceq "repos/$repository/git/ref/tags/$tag") {
        return @{ ref = "refs/tags/$tag"; object = @{ type = $script:remote.TagType; sha = $script:remote.TagCommit } }
    }
    if ($Endpoint -like "repos/$repository/git/tags/*") {
        $type = 'commit'
        if ($script:remote.Nested) { $type = 'tag' }
        return @{ object = @{ type = $type; sha = $script:remote.TagCommit } }
    }
    if ($Endpoint -ceq "repos/$repository/releases/tags/$tag") {
        if (-not $script:remote.Release -and -not $AllowNotFound) { throw 'GitHub API failed (HTTP 404)' }
        return $script:remote.Release
    }
    throw "Unexpected endpoint: $Endpoint"
}
function Get-ReleaseAssets {
    param([string]$Repository, [long]$ReleaseId)
    return ,$script:remote.Assets.ToArray()
}
function Save-ReleaseAsset {
    param([string]$Repository, [long]$AssetId, [string]$Destination)
    if ($script:remote.DownloadFailure) { throw 'Simulated network download failure.' }
    Copy-Item -LiteralPath $script:remote.Files[[int]$AssetId] -Destination $Destination
}
function New-ReleaseRecord {
    param([string]$Repository, [string]$Tag)
    if ($script:remote.Release) { throw 'Duplicate release creation attempted.' }
    $script:remote.CreateCount++
    $script:remote.Release = @{ id = 123; tag_name = $Tag; draft = $false; prerelease = $false; published_at = '2026-01-01'; name = $Tag; body = 'Generated notes' }
}
function Add-ReleaseAsset {
    param([string]$Repository, [long]$ReleaseId, [string]$Path)
    $name = [IO.Path]::GetFileName($Path)
    if (@($script:remote.Assets | Where-Object name -CEQ $name).Count -gt 0) { throw 'Duplicate asset upload attempted.' }
    if ($script:remote.UploadFailureAt -gt 0 -and $script:remote.UploadCount + 1 -eq $script:remote.UploadFailureAt) { throw 'Simulated upload failure.' }
    $script:remote.UploadCount++
    Add-FixtureAsset $name $Path
}
function Invoke-FixturePublish {
    param([string]$Directory = $localBundle, [long]$ReleaseId = 0)
    return Invoke-ReleasePublish -Tag $tag -ExpectedCommit $commit -PackageDirectory $Directory -Repository $repository -ExpectedReleaseId $ReleaseId
}

function Set-FixtureZipEntry {
    param([string]$Directory, [string]$Member, [string]$Text, [switch]$KeepExisting)
    $zipPath = Join-Path $Directory "BloodLine-windows-x64-$tag.zip"
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $old = $zip.GetEntry($Member)
        if ($old -and -not $KeepExisting) { $old.Delete() }
        $entry = $zip.CreateEntry($Member)
        $writer = [IO.StreamWriter]::new($entry.Open(), [Text.UTF8Encoding]::new($false))
        try { $writer.Write($Text) } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
    [IO.File]::WriteAllText("$zipPath.sha256", "$(Get-ReleaseFileHash $zipPath)  $([IO.Path]::GetFileName($zipPath))`n")
}

try {
    $localBundle = New-BundleFixture 'local'
    $previousBundle = New-BundleFixture 'previous-successful-build'
    $bundle = Assert-OfficialReleaseBundle $localBundle $tag $commit
    Assert-Test ((Get-ReleaseFileHash (Join-Path $localBundle $bundle.Names[0])) -cne (Get-ReleaseFileHash (Join-Path $previousBundle $bundle.Names[0]))) 'Fixtures must have different ZIP bytes.'

    Reset-RemoteFixture -Missing
    $result = Invoke-FixturePublish
    Assert-Test ($remote.CreateCount -eq 1 -and $result.UploadedCount -eq 3) 'Missing release is created and receives all three assets.'
    $result = Invoke-FixturePublish
    Assert-Test ($remote.CreateCount -eq 1 -and $remote.UploadCount -eq 3 -and $result.UploadedCount -eq 0) 'Rerun of creation does not mutate.'

    Reset-RemoteFixture
    Add-FixtureAsset 'human-notes.txt' (Join-Path $localBundle $bundle.Names[2])
    $result = Invoke-FixturePublish -ReleaseId 123
    Assert-Test ($remote.CreateCount -eq 0 -and $result.UploadedCount -eq 3 -and $remote.Assets.Count -eq 4) 'Existing empty web release retains unknown assets.'
    Assert-Test ($remote.Release.name -ceq 'Human title' -and $remote.Release.body -ceq 'Human release notes') 'Human title and notes are unchanged.'

    foreach ($count in @(1, 2)) {
        Reset-RemoteFixture
        foreach ($name in $bundle.Names[0..($count - 1)]) { Add-FixtureAsset $name (Join-Path $localBundle $name) }
        $result = Invoke-FixturePublish
        Assert-Test ($result.UploadedCount -eq (3 - $count)) "A matching $count-asset partial release only receives missing assets."
    }
    Reset-RemoteFixture
    Add-FixtureAsset $bundle.Names[0] (Join-Path $previousBundle $bundle.Names[0])
    Assert-Rejected { Invoke-FixturePublish } 'Partial release conflicts*'
    Assert-Test ($remote.UploadCount -eq 0 -and $remote.Assets.Count -eq 1) 'Conflicting partial releases are not changed.'

    Reset-RemoteFixture
    foreach ($name in $bundle.Names) { Add-FixtureAsset $name (Join-Path $previousBundle $name) }
    $result = Invoke-FixturePublish
    Assert-Test ($result.UploadedCount -eq 0 -and $remote.UploadCount -eq 0) 'A complete internally consistent earlier build is a no-op despite byte differences.'

    foreach ($tagType in @('commit', 'tag')) {
        Reset-RemoteFixture
        $remote.TagType = $tagType
        $null = Invoke-FixturePublish
        Assert-Test ($remote.UploadCount -eq 3) "Remote $tagType reference is accepted."
    }
    Reset-RemoteFixture
    $remote.TagCommit = 'b' * 40
    Assert-Rejected { Invoke-FixturePublish } 'Remote tag moved*'
    Assert-Test ($remote.UploadCount -eq 0) 'Moved remote tag stops before uploads.'
    Reset-RemoteFixture
    $remote.TagType = 'tag'; $remote.Nested = $true
    Assert-Rejected { Invoke-FixturePublish } 'Remote tag moved*'

    foreach ($state in @('draft', 'prerelease')) {
        Reset-RemoteFixture
        $remote.Release[$state] = $true
        Assert-Rejected { Invoke-FixturePublish } 'Only an already published regular release*'
        Assert-Test ($remote.UploadCount -eq 0 -and $remote.CreateCount -eq 0) "$state is not published or edited."
    }
    Reset-RemoteFixture
    Assert-Rejected { Invoke-FixturePublish -ReleaseId 999 } 'Release ID changed*'
    Reset-RemoteFixture -Missing
    Assert-Rejected { Invoke-FixturePublish -ReleaseId 123 } 'The release from the triggering event no longer exists*'
    Assert-Test ($remote.CreateCount -eq 0) 'Deleted event release is not recreated.'

    $dirtyBundle = New-BundleFixture 'dirty' -Dirty $true
    Reset-RemoteFixture -Missing
    Assert-Rejected { Invoke-FixturePublish -Directory $dirtyBundle } 'Release metadata must identify*'
    Assert-Test ($remote.ApiCount -eq 0 -and $remote.CreateCount -eq 0) 'Dirty local metadata fails before remote access.'
    $wrongCommitBundle = New-BundleFixture 'wrong-commit' -Commit ('b' * 40)
    Assert-Rejected { Invoke-FixturePublish -Directory $wrongCommitBundle } 'Release metadata must identify*'
    $brokenBundle = New-BundleFixture 'broken-checksum'
    [IO.File]::WriteAllText((Join-Path $brokenBundle $bundle.Names[1]), ('0' * 64) + "  $($bundle.Names[0])`n")
    Assert-Rejected { Invoke-FixturePublish -Directory $brokenBundle } 'Release ZIP checksum*'
    Assert-Test ($remote.CreateCount -eq 0) 'Invalid local bundles never create a release.'

    foreach ($member in @('Object_Connect.exe', 'Resources/data/nodes.csv', 'LICENSES/KamataEngine/ImGui.txt', 'build-info.json')) {
        $modified = New-BundleFixture ('altered-' + [Guid]::NewGuid().ToString('N'))
        Set-FixtureZipEntry $modified $member 'altered bytes, despite valid outer ZIP checksum'
        Assert-Rejected { Invoke-FixturePublish -Directory $modified } 'ZIP content does not match metadata*'
    }
    $duplicate = New-BundleFixture 'duplicate-zip-member'
    Set-FixtureZipEntry $duplicate 'Object_Connect.exe' 'fixture Object_Connect.exe' -KeepExisting
    Assert-Rejected { Invoke-FixturePublish -Directory $duplicate } 'Duplicate or unexpected ZIP member*'
    $unsafe = New-BundleFixture 'unsafe-zip-member'
    Set-FixtureZipEntry $unsafe '../unexpected.txt' 'not extracted'
    Assert-Rejected { Invoke-FixturePublish -Directory $unsafe } 'Unsafe package member path*'
    Assert-Test ($remote.CreateCount -eq 0) 'Invalid ZIP internals cannot create a release, even with a valid ZIP checksum.'

    Reset-RemoteFixture
    foreach ($name in $bundle.Names) { Add-FixtureAsset $name (Join-Path $wrongCommitBundle $name) }
    Assert-Rejected { Invoke-FixturePublish } 'Release metadata must identify*'
    Assert-Test ($remote.UploadCount -eq 0) 'A complete release for another commit is not accepted or changed.'
    Reset-RemoteFixture
    Add-FixtureAsset $bundle.Names[0] (Join-Path $localBundle $bundle.Names[0])
    Add-FixtureAsset $bundle.Names[0] (Join-Path $localBundle $bundle.Names[0])
    Assert-Rejected { Invoke-FixturePublish } 'Conflicting duplicate, case, or incomplete managed asset*'
    Reset-RemoteFixture
    Add-FixtureAsset $bundle.Names[0] (Join-Path $localBundle $bundle.Names[0])
    $remote.Assets[0].state = 'starter'
    Assert-Rejected { Invoke-FixturePublish } 'Conflicting duplicate, case, or incomplete managed asset*'

    Reset-RemoteFixture
    $remote.ReadFailure = 'GitHub API failed (HTTP 403)'
    Assert-Rejected { Invoke-FixturePublish } 'GitHub API failed (HTTP 403)'
    Assert-Test ($remote.CreateCount -eq 0) 'Read/auth failures never imply a missing release.'
    Reset-RemoteFixture
    Add-FixtureAsset $bundle.Names[0] (Join-Path $localBundle $bundle.Names[0])
    $remote.DownloadFailure = $true
    Assert-Rejected { Invoke-FixturePublish } 'Simulated network download failure*'
    Assert-Test ($remote.UploadCount -eq 0) 'Download failures do not modify the release.'
    Reset-RemoteFixture
    $remote.UploadFailureAt = 2
    Assert-Rejected { Invoke-FixturePublish } 'Simulated upload failure*'
    Assert-Test ($remote.Assets.Count -eq 1) 'Upload interruption preserves the completed asset.'
    $remote.UploadFailureAt = 0
    $result = Invoke-FixturePublish
    Assert-Test ($result.UploadedCount -eq 2) 'Retry with original artifact resumes a partial upload.'

    # Exercise the production status parser independently of network adapters.
    $notFound = @{ ExitCode = 1; Output = "HTTP/2.0 404 Not Found`r`nContent-Type: application/json`r`n`r`n{`"message`":`"Not Found`"}"; ErrorOutput = 'HTTP 404' }
    Assert-Test ($null -eq (ConvertFrom-ReleaseApiResponse $notFound -AllowNotFound)) 'Only an explicit HTTP 404 can mean absent.'
    Assert-Rejected { ConvertFrom-ReleaseApiResponse $notFound } 'GitHub API failed (HTTP 404*'
    foreach ($status in @(401, 403, 422, 500, 502)) {
        $response = @{ ExitCode = 1; Output = "HTTP/2.0 $status Failure`nContent-Type: application/json`n`n{}"; ErrorOutput = 'failure' }
        Assert-Rejected { ConvertFrom-ReleaseApiResponse $response -AllowNotFound } "GitHub API failed (HTTP $status*"
    }
    Assert-Rejected { ConvertFrom-ReleaseApiResponse @{ ExitCode = 1; Output = ''; ErrorOutput = 'network unavailable' } -AllowNotFound } 'GitHub API returned no verifiable HTTP response*'
    $success = ConvertFrom-ReleaseApiResponse @{ ExitCode = 0; Output = "HTTP/2.0 200 OK`nContent-Type: application/json`n`n{`"id`":123}"; ErrorOutput = '' }
    Assert-Test ($success.id -eq 123) 'Successful HTTP response is parsed.'
    Assert-Rejected { Invoke-ReleasePublish -Tag ('v' + ('1' * 76) + '.0.0') -ExpectedCommit $commit -PackageDirectory $localBundle -Repository $repository } 'Official versions must use*'

    # Exercise the real API -> JSON -> pagination path with only gh process I/O
    # replaced. Empty/single arrays must not collapse into null or a dictionary.
    & {
        . (Join-Path $PSScriptRoot 'ReleasePublishSupport.ps1')
        function Invoke-ReleaseGh {
            param([string[]]$Arguments, [string]$OutputFile = '')
            Assert-Test ($Arguments[0] -ceq 'api' -and -not $OutputFile) 'Pagination only performs JSON GET calls.'
            $pageMatch = [regex]::Match($Arguments[1], '/assets\?per_page=100&page=([0-9]+)$')
            Assert-Test $pageMatch.Success 'Asset enumeration explicitly requests 100 items and a page.'
            $script:paginationCalls++
            $offset = ([int]$pageMatch.Groups[1].Value - 1) * 100
            $batch = @($script:paginationAssets | Select-Object -Skip $offset -First 100)
            $json = ConvertTo-Json -InputObject $batch -Depth 4 -Compress
            return @{ ExitCode = 0; Output = "HTTP/2.0 200 OK`r`nContent-Type: application/json`r`nX-Test: parser`r`n`r`n$json"; ErrorOutput = '' }
        }
        foreach ($count in @(0, 1, 100, 101, 200)) {
            $script:paginationAssets = @()
            for ($index = 0; $index -lt $count; $index++) { $script:paginationAssets += @{ id = $index + 1; name = "asset-$index"; state = 'uploaded' } }
            $script:paginationCalls = 0
            $assets = Get-ReleaseAssets $repository 123
            Assert-Test ($assets -is [array] -and $assets.Count -eq $count) "Production API/pagination preserves the $count-asset array."
            Assert-Test ($script:paginationCalls -eq ([Math]::Floor($count / 100) + 1)) "Pagination terminates correctly for $count assets."
        }
    }
    Write-Host "Release publisher offline checks passed ($script:assertionCount assertions); no remote I/O performed."
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $targetRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'target')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolved.StartsWith($targetRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^release-publish-tests-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
