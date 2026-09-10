#requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleaseBuildSupport.ps1')
$testRoot = Join-Path (Split-Path -Parent $PSScriptRoot) ('target/release-build-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$tag = 'v1.2.3'
$commit = 'a' * 40
$repository = 'test-owner/test-repo'
$script:assertionCount = 0
$script:mutationCount = 0

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

function New-BuildBundleFixture {
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
        History = [Collections.Generic.List[object]]::new()
        ApiCount = 0; AssetListCount = 0; HistoryCount = 0; DownloadCount = 0
        TagType = 'commit'; TagCommit = $commit; Nested = $false
        ReadFailure = 0; NetworkFailure = $false; HistoryFailure = 0; DownloadFailure = $false
        DuringVerification = ''
    }
    if ($Missing) { $script:remote.Release = $null }
}

function Add-FixtureAsset {
    param([string]$Name, [string]$Path)
    $script:remote.NextId++
    $script:remote.Assets.Add(@{
        id = $script:remote.NextId; name = $Name; state = 'uploaded'
        size = (Get-Item -LiteralPath $Path).Length; updated_at = '2026-01-01T00:00:00Z'
        digest = 'sha256:' + (Get-ReleaseFileHash $Path)
    })
    $script:remote.Files[$script:remote.NextId] = $Path
}

function Add-FixtureSuccess {
    $script:remote.History.Add(@{ head_sha = $commit; head_branch = $tag; event = 'push'; status = 'completed'; conclusion = 'success' })
}

function Set-FixtureComplete {
    param([string]$Directory = $localBundle, [switch]$NoSuccess)
    if (-not $script:remote.Release) {
        $script:remote.Release = @{ id = 123; tag_name = $tag; draft = $false; prerelease = $false; published_at = '2026-01-01'; name = 'Human title'; body = 'Human release notes' }
    }
    foreach ($name in $names) { Add-FixtureAsset $name (Join-Path $Directory $name) }
    if (-not $NoSuccess) { Add-FixtureSuccess }
}

function New-FixtureResponse {
    param([object]$Body, [int]$Status = 200)
    $code = 0
    if ($Status -ge 400) { $code = 1 }
    $json = ConvertTo-Json -InputObject $Body -Depth 12 -Compress
    return @{ ExitCode = $code; Output = "HTTP/2.0 $Status Fixture`r`nContent-Type: application/json`r`n`r`n$json"; ErrorOutput = "HTTP $Status" }
}

# Keep the production API parsing, pagination, download and bundle validators;
# replace only process I/O. Any non-GET gh command is an immediate test failure.
function Invoke-ReleaseGh {
    param([string[]]$Arguments, [string]$OutputFile = '')
    if ($Arguments[0] -cne 'api' -or $Arguments -contains '--method' -or $Arguments -contains '--input') {
        $script:mutationCount++
        throw 'Unexpected remote mutation in the read-only build gate.'
    }
    $script:remote.ApiCount++
    $endpoint = $Arguments[1]
    if ($script:remote.NetworkFailure) { return @{ ExitCode = 1; Output = ''; ErrorOutput = 'network unavailable' } }
    if ($script:remote.ReadFailure) { return New-FixtureResponse @{ message = 'failure' } $script:remote.ReadFailure }
    if ($OutputFile) {
        $script:remote.DownloadCount++
        if ($script:remote.DownloadFailure) { return @{ ExitCode = 1; Output = ''; ErrorOutput = 'download unavailable' } }
        $match = [regex]::Match($endpoint, '^repos/' + [regex]::Escape($repository) + '/releases/assets/([0-9]+)$')
        if (-not $match.Success) { throw "Unexpected download endpoint: $endpoint" }
        Copy-Item -LiteralPath $script:remote.Files[[int]$match.Groups[1].Value] -Destination $OutputFile
        return @{ ExitCode = 0; Output = ''; ErrorOutput = '' }
    }
    if ($endpoint -ceq "repos/$repository/git/ref/tags/$tag") {
        if ($script:remote.DownloadCount -eq 3 -and $script:remote.DuringVerification -ceq 'tag') { $script:remote.TagCommit = 'b' * 40 }
        return New-FixtureResponse @{ ref = "refs/tags/$tag"; object = @{ type = $script:remote.TagType; sha = $script:remote.TagCommit } }
    }
    if ($endpoint -like "repos/$repository/git/tags/*") {
        $type = 'commit'
        if ($script:remote.Nested) { $type = 'tag' }
        return New-FixtureResponse @{ object = @{ type = $type; sha = $script:remote.TagCommit } }
    }
    if ($endpoint -ceq "repos/$repository/releases/tags/$tag") {
        if ($script:remote.DownloadCount -eq 3 -and $script:remote.DuringVerification -ceq 'release') { $script:remote.Release.id = 456 }
        if (-not $script:remote.Release) { return New-FixtureResponse @{ message = 'Not Found' } 404 }
        return New-FixtureResponse $script:remote.Release
    }
    if ($endpoint -ceq "repos/$repository/releases/123/assets?per_page=100&page=1") {
        $script:remote.AssetListCount++
        if ($script:remote.AssetListCount -eq 2) {
            switch ($script:remote.DuringVerification) {
                'asset-id' { $script:remote.Assets[0].id = 9999 }
                'asset-size' { $script:remote.Assets[0].size++ }
                'asset-time' { $script:remote.Assets[0].updated_at = '2026-02-01T00:00:00Z' }
                'asset-digest' { $script:remote.Assets[0].digest = 'sha256:' + ('b' * 64) }
                'asset-deleted' { $script:remote.Assets.RemoveAt(0) }
            }
        }
        return New-FixtureResponse $script:remote.Assets.ToArray()
    }
    $historyPrefix = "repos/$repository/actions/workflows/windows-build.yml/runs?head_sha=$commit&branch=$tag&status=success&per_page=100&page="
    if ($endpoint.StartsWith($historyPrefix, [StringComparison]::Ordinal)) {
        $script:remote.HistoryCount++
        if ($script:remote.HistoryFailure) { return New-FixtureResponse @{ message = 'history forbidden' } $script:remote.HistoryFailure }
        $page = [int]$endpoint.Substring($historyPrefix.Length)
        $batch = @($script:remote.History | Select-Object -Skip (($page - 1) * 100) -First 100)
        return New-FixtureResponse @{ workflow_runs = $batch }
    }
    throw "Unexpected read-only endpoint: $endpoint"
}
function New-ReleaseRecord { $script:mutationCount++; throw 'Build gate must not create a release.' }
function Add-ReleaseAsset { $script:mutationCount++; throw 'Build gate must not upload an asset.' }

function Invoke-FixtureDecision {
    param([long]$ReleaseId = 0)
    return Get-ReleaseBuildDecision -Publish $true -Tag $tag -ExpectedCommit $commit -Repository $repository -ExpectedReleaseId $ReleaseId
}

try {
    $localBundle = New-BuildBundleFixture 'valid'
    $baseName = "BloodLine-windows-x64-$tag"
    $names = @("$baseName.zip", "$baseName.zip.sha256", "$baseName.build-info.json")
    $null = Assert-OfficialReleaseBundle $localBundle $tag $commit

    Reset-RemoteFixture
    $result = Get-ReleaseBuildDecision -Publish $false
    Assert-Test ($result.NeedsBuild -and $remote.ApiCount -eq 0) 'Ordinary push, PR and manual events require a build without any remote I/O.'
    foreach ($invalidTag in @('v1.1', 'V1.2.3', 'v01.2.3', 'v1.2.3-rc1')) {
        Assert-Rejected { Get-ReleaseBuildDecision -Publish $true -Tag $invalidTag -ExpectedCommit $commit -Repository $repository } 'Official versions must use*'
    }
    Assert-Rejected { Get-ReleaseBuildDecision -Publish $true -Tag $tag -ExpectedCommit 'abc' -Repository $repository } 'Invalid release version*'
    Assert-Rejected { Get-ReleaseBuildDecision -Publish $true -Tag $tag -ExpectedCommit $commit -Repository 'owner/repo/extra' } 'Invalid release version*'
    Assert-Rejected { Invoke-FixtureDecision -ReleaseId -1 } 'Invalid release version*'
    Assert-Test ($remote.ApiCount -eq 0) 'Invalid release arguments are rejected before remote I/O.'

    Reset-RemoteFixture -Missing
    Assert-Test (Invoke-FixtureDecision).NeedsBuild 'A new tag with no Release requires a build.'
    Assert-Rejected { Invoke-FixtureDecision -ReleaseId 123 } 'The release from the triggering event no longer exists*'
    Reset-RemoteFixture
    Assert-Rejected { Invoke-FixtureDecision -ReleaseId 456 } 'Release ID changed*'
    foreach ($state in @('draft', 'prerelease')) {
        Reset-RemoteFixture
        $remote.Release[$state] = $true
        Assert-Rejected { Invoke-FixtureDecision } 'Only an already published regular release*'
    }
    foreach ($count in @(0, 1, 2)) {
        Reset-RemoteFixture
        for ($index = 0; $index -lt $count; $index++) { Add-FixtureAsset $names[$index] (Join-Path $localBundle $names[$index]) }
        $result = Invoke-FixtureDecision -ReleaseId 123
        Assert-Test ($result.NeedsBuild -and $remote.DownloadCount -eq 0) "A $count-asset partial release does not count as a successful build."
    }

    # Workflow-level concurrency serializes these calls. First success creates
    # the immutable bundle and green run; the second event must not build again.
    foreach ($order in @('push-first', 'published-first')) {
        $buildCount = 0
        $firstId = 0L
        $secondId = 123L
        if ($order -ceq 'push-first') { Reset-RemoteFixture -Missing }
        else { Reset-RemoteFixture; $firstId = 123L; $secondId = 0L }
        if ((Invoke-FixtureDecision -ReleaseId $firstId).NeedsBuild) { $buildCount++; Set-FixtureComplete }
        if ((Invoke-FixtureDecision -ReleaseId $secondId).NeedsBuild) { $buildCount++ }
        Assert-Test ($buildCount -eq 1) "$order builds once across tag push and published events."
        Assert-Test ($remote.DownloadCount -eq 3 -and $remote.HistoryCount -eq 1 -and $remote.AssetListCount -ge 2) "$order verifies bytes, successful CI and unchanged identities before skipping."
    }
    Reset-RemoteFixture
    Assert-Test (Invoke-FixtureDecision).NeedsBuild 'A first attempt before compilation needs a build.'
    Assert-Test (Invoke-FixtureDecision).NeedsBuild 'An earlier failed build leaves no completion marker and does not suppress the next build.'

    Reset-RemoteFixture
    Set-FixtureComplete -NoSuccess
    $result = Invoke-FixtureDecision
    Assert-Test ($result.NeedsBuild -and $result.Reason -like '*no successful release workflow*') 'Even a complete valid package needs build/test gates when no successful same-tag run exists.'
    foreach ($change in @(@{ head_branch = 'master' }, @{ head_sha = ('b' * 40) }, @{ event = 'workflow_dispatch' }, @{ status = 'in_progress' }, @{ conclusion = 'failure' })) {
        Reset-RemoteFixture
        Set-FixtureComplete
        foreach ($key in $change.Keys) { $remote.History[0][$key] = $change[$key] }
        Assert-Test (Invoke-FixtureDecision).NeedsBuild 'An unrelated, incomplete or failed workflow is not successful release evidence.'
    }
    Reset-RemoteFixture
    Set-FixtureComplete -NoSuccess
    for ($index = 0; $index -lt 100; $index++) { $remote.History.Add(@{ head_sha = $commit; head_branch = 'master'; event = 'push'; status = 'completed'; conclusion = 'success' }) }
    Add-FixtureSuccess
    Assert-Test (-not (Invoke-FixtureDecision).NeedsBuild -and $remote.HistoryCount -eq 2) 'Release history is paginated, not limited to the first 100 results.'

    foreach ($tagType in @('commit', 'tag')) {
        Reset-RemoteFixture
        Set-FixtureComplete
        $remote.TagType = $tagType
        Assert-Test (-not (Invoke-FixtureDecision).NeedsBuild) "A matching remote $tagType tag can reuse verified release assets."
    }
    Reset-RemoteFixture
    $remote.TagCommit = 'b' * 40
    Assert-Rejected { Invoke-FixtureDecision } 'Remote tag moved*'
    Reset-RemoteFixture
    $remote.TagType = 'tag'; $remote.Nested = $true
    Assert-Rejected { Invoke-FixtureDecision } 'Remote tag moved*'
    foreach ($change in @('tag', 'release', 'asset-id', 'asset-size', 'asset-time', 'asset-digest', 'asset-deleted')) {
        Reset-RemoteFixture
        Set-FixtureComplete
        $remote.DuringVerification = $change
        $pattern = 'Managed release asset*changed*'
        if ($change -ceq 'tag') { $pattern = 'Remote tag moved*' }
        if ($change -ceq 'release') { $pattern = 'Release ID changed*' }
        Assert-Rejected { Invoke-FixtureDecision } $pattern
    }

    foreach ($property in @(@{ id = 0 }, @{ size = 0 }, @{ updated_at = 'not-a-time' }, @{ digest = 'not-a-digest' })) {
        Reset-RemoteFixture
        Add-FixtureAsset $names[0] (Join-Path $localBundle $names[0])
        foreach ($key in $property.Keys) { $remote.Assets[0][$key] = $property[$key] }
        Assert-Rejected { Invoke-FixtureDecision } 'Malformed managed release asset*'
    }
    foreach ($conflict in @('duplicate', 'case', 'starter', 'duplicate-id')) {
        Reset-RemoteFixture
        Add-FixtureAsset $names[0] (Join-Path $localBundle $names[0])
        $pattern = 'Conflicting duplicate, case, or incomplete managed asset*'
        switch ($conflict) {
            'duplicate' { Add-FixtureAsset $names[0] (Join-Path $localBundle $names[0]) }
            'case' { $remote.Assets[0].name = $names[0].ToUpperInvariant() }
            'starter' { $remote.Assets[0].state = 'starter' }
            'duplicate-id' {
                Add-FixtureAsset $names[1] (Join-Path $localBundle $names[1])
                $remote.Assets[1].id = $remote.Assets[0].id
                $pattern = 'Malformed managed release asset*'
            }
        }
        Assert-Rejected { Invoke-FixtureDecision } $pattern
    }
    Reset-RemoteFixture
    Set-FixtureComplete
    foreach ($asset in $remote.Assets) { $asset.Remove('digest') }
    Add-FixtureAsset 'human-notes.txt' (Join-Path $localBundle $names[2])
    Assert-Test (-not (Invoke-FixtureDecision).NeedsBuild) 'Older assets without GitHub digest and unrelated human assets are supported; full local hash verification still runs.'
    Assert-Test ($remote.Release.name -ceq 'Human title' -and $remote.Release.body -ceq 'Human release notes' -and $remote.Assets.Count -eq 4) 'Read-only dedup preserves release prose and all assets.'

    foreach ($state in @('wrong-commit', 'dirty')) {
        $fixtureCommit = $commit
        if ($state -ceq 'wrong-commit') { $fixtureCommit = 'b' * 40 }
        $fixture = New-BuildBundleFixture $state -Commit $fixtureCommit -Dirty ($state -ceq 'dirty')
        Reset-RemoteFixture
        Set-FixtureComplete -Directory $fixture
        Assert-Rejected { Invoke-FixtureDecision } 'Release metadata must identify*'
    }
    $corrupt = New-BuildBundleFixture 'corrupt-checksum'
    [IO.File]::WriteAllText((Join-Path $corrupt $names[1]), ('0' * 64) + "  $($names[0])`n")
    Reset-RemoteFixture
    Set-FixtureComplete -Directory $corrupt
    Assert-Rejected { Invoke-FixtureDecision } 'Release ZIP checksum*'
    $corrupt = New-BuildBundleFixture 'corrupt-content'
    $zipPath = Join-Path $corrupt $names[0]
    $zip = [IO.Compression.ZipFile]::Open($zipPath, [IO.Compression.ZipArchiveMode]::Update)
    try {
        $zip.GetEntry('Object_Connect.exe').Delete()
        $writer = [IO.StreamWriter]::new($zip.CreateEntry('Object_Connect.exe').Open())
        try { $writer.Write('modified executable') } finally { $writer.Dispose() }
    } finally { $zip.Dispose() }
    [IO.File]::WriteAllText("$zipPath.sha256", "$(Get-ReleaseFileHash $zipPath)  $($names[0])`n")
    Reset-RemoteFixture
    Set-FixtureComplete -Directory $corrupt
    Assert-Rejected { Invoke-FixtureDecision } 'ZIP content does not match metadata*'
    Reset-RemoteFixture
    Set-FixtureComplete
    $remote.Assets[0].size++
    Assert-Rejected { Invoke-FixtureDecision } 'Downloaded release asset size changed*'
    Reset-RemoteFixture
    Set-FixtureComplete
    $remote.Assets[0].digest = 'sha256:' + ('b' * 64)
    Assert-Rejected { Invoke-FixtureDecision } 'Downloaded release asset digest changed*'

    foreach ($status in @(401, 403, 500)) {
        Reset-RemoteFixture
        $remote.ReadFailure = $status
        Assert-Rejected { Invoke-FixtureDecision } "GitHub API failed (HTTP $status*"
    }
    Reset-RemoteFixture
    $remote.NetworkFailure = $true
    Assert-Rejected { Invoke-FixtureDecision } 'GitHub API returned no verifiable HTTP response*'
    Reset-RemoteFixture
    Set-FixtureComplete
    $remote.DownloadFailure = $true
    Assert-Rejected { Invoke-FixtureDecision } 'Downloading release asset*failed*'
    Reset-RemoteFixture
    Set-FixtureComplete
    $remote.HistoryFailure = 403
    Assert-Rejected { Invoke-FixtureDecision } 'GitHub API failed (HTTP 403*'

    Assert-Test ($script:mutationCount -eq 0) 'The production build gate never creates, edits, deletes or uploads remote content.'
    Write-Host "Release pre-build dedup offline checks passed ($script:assertionCount assertions); no remote I/O performed."
} finally {
    $resolved = [IO.Path]::GetFullPath($testRoot)
    $targetRoot = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) 'target')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolved.StartsWith($targetRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^release-build-tests-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
