[CmdletBinding()]
param([string]$InputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'target/Release'))
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'PackageSupport.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$testRoot = Join-Path $repoRoot ('target/package-tests-' + [Guid]::NewGuid().ToString('N'))
$fixture = Join-Path $testRoot 'input'
$output = Join-Path $testRoot 'output'
$packageScript = Join-Path $repoRoot 'Package.ps1'
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $InputDirectory 'Object_Connect.exe'), (Join-Path $InputDirectory 'build-info.json') -Destination $fixture
Copy-Item -LiteralPath (Join-Path $InputDirectory 'Resources') -Destination $fixture -Recurse
function Assert-PackageRejected {
    param([scriptblock]$Action, [string]$ExpectedMessage)
    $errorMessage = ''
    try { & $Action | Out-Null } catch { $errorMessage = $_.Exception.Message }
    if ($errorMessage -notlike $ExpectedMessage) { throw "Expected '$ExpectedMessage', received '$errorMessage'." }
    Write-Host "PASS rejection: $ExpectedMessage"
}
$metadataPath = Join-Path $fixture 'build-info.json'
$originalMetadata = [IO.File]::ReadAllText($metadataPath)
$originalCi = [Environment]::GetEnvironmentVariable('GITHUB_ACTIONS')
try {
    & $packageScript -Version integration-test -InputDirectory $fixture -OutputDirectory $output
    $zipPath = Join-Path $output 'BloodLine-windows-x64-integration-test.zip'
    $checksum = ([IO.File]::ReadAllText("$zipPath.sha256")).Split(' ')[0]
    if ($checksum -ne (Get-FileHash -LiteralPath $zipPath).Hash) { throw 'Package checksum verification failed.' }
    $extracted = Join-Path $testRoot 'extracted'
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($zipPath, $extracted)
    Assert-ResourceManifestsMatch (Get-ResourceManifest (Join-Path $fixture 'Resources')) (Get-ResourceManifest (Join-Path $extracted 'Resources'))
    if ((Get-FileHash -LiteralPath (Join-Path $fixture 'Object_Connect.exe')).Hash -ne
        (Get-FileHash -LiteralPath (Join-Path $extracted 'Object_Connect.exe')).Hash) { throw 'Packaged EXE hash differs.' }
    $badContents = @(Get-ChildItem -LiteralPath $extracted -Recurse -File | Where-Object { $_.Extension -match '^\.(pdb|lib|dll)$' })
    if ($badContents.Count -gt 0) { throw 'Development binaries leaked into ZIP.' }
    foreach ($notice in @('LICENSES/BIZUDPGothic-OFL.txt', 'LICENSES/KamataEngine/ImGui.txt', 'build-info.json')) {
        if (-not (Test-Path -LiteralPath (Join-Path $extracted $notice))) { throw "Missing packaged notice: $notice" }
    }
    Write-Host 'PASS ZIP extraction, resources, EXE, checksum, font notice and binary exclusions.'
    Assert-PackageRejected { & $packageScript -Version integration-test -InputDirectory $fixture -OutputDirectory $output } 'Refusing to overwrite*'
    $metadata = $originalMetadata | ConvertFrom-Json
    $metadata.configuration = 'Debug'
    [IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 8))
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'Only a Release x64*'
    $metadata.configuration = 'Release'
    $metadata.commit = $null
    [IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 8))
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'Build provenance is unknown*'
    [IO.File]::WriteAllText($metadataPath, $originalMetadata)
    $exePath = Join-Path $fixture 'Object_Connect.exe'
    $exeStream = [IO.File]::OpenWrite($exePath)
    try { $exeStream.WriteByte(0) } finally { $exeStream.Dispose() }
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'EXE differs from its link metadata*'
    Copy-Item -LiteralPath (Join-Path $InputDirectory 'Object_Connect.exe') -Destination $exePath -Force
    $resourcePath = Join-Path $fixture 'Resources/white1x1.png'
    [IO.File]::WriteAllText($resourcePath, 'modified fixture')
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'Resource missing or modified*'
    Copy-Item -LiteralPath (Join-Path $InputDirectory 'Resources/white1x1.png') -Destination $resourcePath -Force
    Remove-Item -LiteralPath $resourcePath
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'Resource count mismatch*'
    Copy-Item -LiteralPath (Join-Path $InputDirectory 'Resources/white1x1.png') -Destination $resourcePath
    $sidecar = Join-Path $fixture 'dxcompiler.dll'
    [IO.File]::WriteAllText($sidecar, 'debug fixture')
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'Debug sidecar found*'
    Remove-Item -LiteralPath $sidecar
    Assert-PackageRejected { & $packageScript -Version v01.2.3 -InputDirectory $fixture -OutputDirectory $output } 'Official versions must use*'
    & (Join-Path $PSScriptRoot 'Write-BuildInfo.ps1') -Executable $exePath -SourceDirectory $fixture `
        -Configuration Release -CompilerVersion fixture -Generator fixture -CMakeVersion fixture
    $archiveMetadata = Get-Content -LiteralPath $metadataPath -Raw | ConvertFrom-Json
    if ($null -ne $archiveMetadata.commit -or $null -ne $archiveMetadata.sourceDirty) {
        throw 'A source archive directory must not inherit its parent repository commit.'
    }
    Write-Host 'PASS source archive metadata remains unknown, without breaking a local build.'
    $metadata = $originalMetadata | ConvertFrom-Json
    $metadata.sourceDirty = $true
    [IO.File]::WriteAllText($metadataPath, ($metadata | ConvertTo-Json -Depth 8))
    $env:GITHUB_ACTIONS = 'true'
    Assert-PackageRejected { & $packageScript -InputDirectory $fixture -OutputDirectory $output } 'CI must package an EXE*'
    Write-Host 'Package integration checks passed.'
}
finally {
    [Environment]::SetEnvironmentVariable('GITHUB_ACTIONS', $originalCi, 'Process')
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $targetRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot 'target')).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if ($resolvedTestRoot.StartsWith($targetRoot, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedTestRoot) -match '^package-tests-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}
