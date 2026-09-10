[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'ReleasePolicy.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot ('target/release-policy-tests-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$script:assertions = 0
function Assert-Policy {
    param([bool]$Condition, [string]$Name)
    if (-not $Condition) { throw "Policy assertion failed: $Name" }
    $script:assertions++
}
function Assert-PolicyRejected {
    param([scriptblock]$Action, [string]$Message)
    $caught = ''
    try { & $Action | Out-Null } catch { $caught = $_.Exception.Message }
    Assert-Policy ($caught -like $Message) "expected '$Message'; got '$caught'"
}
function Invoke-FixtureGit {
    param([string[]]$Arguments)
    $result = @(& git -C $testRoot -c user.name=release-policy-test -c user.email=release-policy@example.invalid `
        -c commit.gpgsign=false -c tag.gpgsign=false -c advice.nestedTag=false -c core.hooksPath=$emptyHooks @Arguments)
    if ($LASTEXITCODE -ne 0) { throw "Fixture git command failed: $Arguments" }
    return ($result -join "`n").Trim()
}
function Write-EventFixture {
    param([object]$Payload)
    [IO.File]::WriteAllText($eventFile, ($Payload | ConvertTo-Json -Depth 6), [Text.UTF8Encoding]::new($false))
}
try {
    $eventFile = Join-Path $testRoot 'event.json'
    $sha = '0123456789abcdef0123456789abcdef01234567'
    $baseArguments = @{ EventPath = $eventFile; Sha = $sha }
    Write-EventFixture @{}
    foreach ($event in @('push', 'pull_request', 'workflow_dispatch')) {
        $context = Get-ReleaseEventContext @baseArguments -EventName $event -Ref 'refs/heads/master'
        Assert-Policy (-not $context.Publish -and $context.Commit -ceq $sha -and -not $context.Tag) "$event stays artifact-only"
    }
    $context = Get-ReleaseEventContext @baseArguments -EventName workflow_dispatch -Ref 'refs/tags/v1.2.3'
    Assert-Policy (-not $context.Publish) 'Run workflow on a tag does not publish'
    $context = Get-ReleaseEventContext @baseArguments -EventName push -Ref 'refs/tags/v1.2.3'
    Assert-Policy ($context.Publish -and $context.Tag -ceq 'v1.2.3' -and -not $context.ReleaseId) 'tag push publishes'
    $releasePayload = @{ action = 'published'; release = @{ tag_name = 'v1.2.3'; id = 123456789; draft = $false; prerelease = $false; target_commitish = 'moving-branch' } }
    Write-EventFixture $releasePayload
    $context = Get-ReleaseEventContext @baseArguments -EventName release -Ref 'refs/tags/v1.2.3'
    Assert-Policy ($context.Publish -and $context.Tag -ceq 'v1.2.3' -and $context.ReleaseId -ceq '123456789') 'published UI release publishes'
    Assert-Policy ($context.Commit -ceq $sha) 'release checkout uses event SHA, not target_commitish'
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName release -Ref 'refs/tags/v1.2.4' } '*ref do not match*'
    $releasePayload.action = 'edited'
    Write-EventFixture $releasePayload
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName release -Ref 'refs/tags/v1.2.3' } '*Only release.published*'
    $releasePayload.action = 'published'
    foreach ($field in @('draft', 'prerelease')) {
        $releasePayload.release[$field] = $true
        Write-EventFixture $releasePayload
        Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName release -Ref 'refs/tags/v1.2.3' } '*Only published stable releases*'
        $releasePayload.release[$field] = $false
    }
    $releasePayload.release.id = 0
    Write-EventFixture $releasePayload
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName release -Ref 'refs/tags/v1.2.3' } '*Release ID*'
    foreach ($version in @('v1.1', 'v01.2.3', 'V1.2.3', 'v1.2.3-rc.1', 'v1.2.3+build', 'v1.2.3/extra', ('v1.2.' + ('1' * 80)))) {
        Write-EventFixture @{}
        Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName push -Ref "refs/tags/$version" } 'Official versions must use*'
        $releasePayload.release.id = 1
        $releasePayload.release.tag_name = $version
        Write-EventFixture $releasePayload
        Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName release -Ref "refs/tags/$version" } 'Official versions must use*'
    }
    Write-EventFixture @{ deleted = $true }
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName push -Ref 'refs/tags/v1.2.3' } '*Deleted refs*'
    Write-EventFixture @{}
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName push -Ref 'refs/heads/feature' } '*limited to master*'
    Assert-PolicyRejected { Get-ReleaseEventContext @baseArguments -EventName schedule -Ref 'refs/heads/master' } '*Unsupported CI event*'
    Assert-PolicyRejected { Get-ReleaseEventContext -EventPath $eventFile -Sha 'abc' -EventName push -Ref 'refs/tags/v1.2.3' } '*full Git commit SHA*'

    # Isolated fixture history only: no commits, tags, remotes or config are
    # created in the real project repository, and nothing is pushed.
    $emptyHooks = Join-Path $testRoot 'empty-hooks'
    New-Item -ItemType Directory -Path $emptyHooks | Out-Null
    $null = Invoke-FixtureGit @('init', '--quiet', '--initial-branch=master', "--template=$emptyHooks")
    $null = Invoke-FixtureGit @('commit', '--quiet', '--allow-empty', '-m', 'fixture base')
    $baseCommit = Invoke-FixtureGit @('rev-parse', 'HEAD')
    $null = Invoke-FixtureGit @('update-ref', 'refs/remotes/origin/master', $baseCommit)
    $null = Invoke-FixtureGit @('tag', 'v0.0.1')
    $null = Invoke-FixtureGit @('tag', '-a', 'v0.0.2', '-m', 'annotated fixture')
    foreach ($tag in @('v0.0.1', 'v0.0.2')) {
        Assert-Policy ((Get-LocalReleaseTagCommit $testRoot $tag) -ceq $baseCommit) "$tag resolves the same commit"
        & (Join-Path $PSScriptRoot 'Assert-ReleaseTag.ps1') -Tag $tag -SourceDirectory $testRoot -ExpectedCommit $baseCommit
        $script:assertions++
    }
    Assert-PolicyRejected { Get-LocalReleaseTagCommit $testRoot 'v0.0.99' } '*tag does not exist*'
    Assert-PolicyRejected { & (Join-Path $PSScriptRoot 'Assert-ReleaseTag.ps1') -Tag 'v0.0.1' -SourceDirectory $testRoot -ExpectedCommit $sha } '*does not match the event commit*'
    $null = Invoke-FixtureGit @('tag', '-a', 'v0.0.3', 'v0.0.2', '-m', 'nested fixture')
    Assert-PolicyRejected { Get-LocalReleaseTagCommit $testRoot 'v0.0.3' } '*directly to a commit*'
    $tree = Invoke-FixtureGit @('rev-parse', 'HEAD^{tree}')
    $null = Invoke-FixtureGit @('tag', 'v0.0.4', $tree)
    Assert-PolicyRejected { Get-LocalReleaseTagCommit $testRoot 'v0.0.4' } '*must point to a commit*'
    $null = Invoke-FixtureGit @('commit', '--quiet', '--allow-empty', '-m', 'unmerged fixture')
    $null = Invoke-FixtureGit @('tag', 'v0.0.5')
    Assert-PolicyRejected { & (Join-Path $PSScriptRoot 'Assert-ReleaseTag.ps1') -Tag 'v0.0.1' -SourceDirectory $testRoot } '*Checkout does not match*'
    Assert-PolicyRejected { & (Join-Path $PSScriptRoot 'Assert-ReleaseTag.ps1') -Tag 'v0.0.5' -SourceDirectory $testRoot } '*must already belong to origin/master*'
    Write-Host "Release policies: $script:assertions assertions passed (offline events and isolated Git fixtures)."
} finally {
    $targetRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'target')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    $resolved = [IO.Path]::GetFullPath($testRoot)
    if ($resolved.StartsWith($targetRoot, [StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetFileName($resolved) -match '^release-policy-tests-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
# Expected failing Git commands above must not leak exit 1 into the GitHub
# Actions pwsh wrapper after all assertions pass. Never reset it in finally:
# an unexpected test failure must still fail the workflow.
exit 0
