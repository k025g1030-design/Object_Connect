[CmdletBinding()]
param(
    [string]$Version = '',
    [string]$InputDirectory = (Join-Path $PSScriptRoot 'target/Release'),
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'target/packages')
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'scripts/BuildTools.ps1')
. (Join-Path $PSScriptRoot 'scripts/PackageSupport.ps1')
. (Join-Path $PSScriptRoot 'scripts/ReleasePolicy.ps1')

$inputPath = (Resolve-Path -LiteralPath $InputDirectory).ProviderPath
$exe = Join-Path $inputPath 'Object_Connect.exe'
$infoPath = Join-Path $inputPath 'build-info.json'
foreach ($required in @($exe, $infoPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required build output is missing: $required. Rebuild Release." }
}
$buildInfo = Get-Content -LiteralPath $infoPath -Raw | ConvertFrom-Json
if ($buildInfo.schemaVersion -ne 1 -or $buildInfo.configuration -cne 'Release' -or $buildInfo.architecture -cne 'x64') {
    throw 'Only a Release x64 build with supported link provenance may be packaged.'
}
if (-not $buildInfo.commit -or $buildInfo.commit -notmatch '^[a-f0-9]{40}$' -or $null -eq $buildInfo.sourceDirty) {
    throw 'Build provenance is unknown. Rebuild Release from a Git checkout before packaging.'
}
if ($buildInfo.executableSha256 -ne (Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash) {
    throw 'EXE differs from its link metadata. Rebuild Release before packaging.'
}
$sourceCommit = (& git -C $PSScriptRoot rev-parse --verify HEAD | Out-String).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify the packaging source commit.' }
$sourceStatus = @(& git -C $PSScriptRoot status --porcelain --untracked-files=normal)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the packaging source state.' }
$sourceDirty = ($sourceStatus.Count -gt 0)
if (-not $Version) { $Version = $buildInfo.commit.Substring(0, 12) }
if ($Version -notmatch '^[a-zA-Z0-9][a-zA-Z0-9._-]{0,79}$') { throw 'Version must be a filename-safe label of 1-80 characters.' }
if ($Version -match '^v') {
    Assert-OfficialReleaseVersion $Version
    $tagCommit = Get-LocalReleaseTagCommit -SourceDirectory $PSScriptRoot -Tag $Version
    if ($tagCommit -ne $buildInfo.commit -or $tagCommit -ne $sourceCommit -or $buildInfo.sourceDirty -or $sourceDirty) {
        throw 'Official tag, linked EXE, and clean packaging checkout must identify the same commit.'
    }
}
if ($env:GITHUB_ACTIONS -eq 'true' -and ($buildInfo.commit -ne $sourceCommit -or $buildInfo.sourceDirty -or $sourceDirty)) {
    throw 'CI must package an EXE linked from this exact clean checkout.'
}
$sourceResources = Get-ResourceManifest (Join-Path $PSScriptRoot 'NoviceResources')
$deployedResources = Get-ResourceManifest (Join-Path $inputPath 'Resources')
Assert-ResourceManifestsMatch $sourceResources $deployedResources
foreach ($requiredResource in @('fonts/BIZUDPGothic-Regular.ttf', 'data/levels.csv', 'data/nodes.csv')) {
    if (-not $sourceResources.Contains($requiredResource)) { throw "Required resource is missing: $requiredResource" }
}
$unexpectedBinaries = @(Get-ChildItem -LiteralPath (Join-Path $inputPath 'Resources') -Recurse -File -Force |
    Where-Object { $_.Extension -match '^\.(dll|exe|pdb|lib)$' })
if ($unexpectedBinaries.Count -gt 0) { throw 'Resources must not contain development binaries or DLL sidecars.' }
foreach ($sidecar in @('dxcompiler.dll', 'dxil.dll')) {
    if (Test-Path -LiteralPath (Join-Path $inputPath $sidecar)) { throw "Debug sidecar found in Release output: $sidecar. Rebuild Release." }
}
$dumpbin = Get-DumpbinExecutable
$importReport = & $dumpbin /nologo /dependents $exe 2>&1 | Out-String
if ($LASTEXITCODE -ne 0) { throw 'dumpbin failed; no package was created.' }
$dependencies = @(Assert-ReleaseDependencies $importReport)
$licenseSource = Join-Path $PSScriptRoot 'third_party/KamataEngine/LICENSES'
if (-not (Test-Path -LiteralPath $licenseSource -PathType Container)) { throw "Third-party notices are missing: $licenseSource" }
$licenses = Get-ResourceManifest $licenseSource
$projectLicenseSource = Join-Path $PSScriptRoot 'LICENSES'
$projectLicenses = Get-ResourceManifest $projectLicenseSource
$packageInfo = [ordered]@{
    schemaVersion = 1
    version = $Version
    built = $buildInfo
    packagedAtUtc = [DateTime]::UtcNow.ToString('o')
    resourceSourceCommit = $sourceCommit
    resourceSourceDirty = $sourceDirty
    resources = $deployedResources
    licenses = $licenses
    projectLicenses = $projectLicenses
    directDllDependencies = $dependencies
}
$outputPath = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$baseName = "BloodLine-windows-x64-$Version"
$zipPath = Join-Path $outputPath "$baseName.zip"
$checksumPath = "$zipPath.sha256"
$externalInfoPath = Join-Path $outputPath "$baseName.build-info.json"
foreach ($destination in @($zipPath, $checksumPath, $externalInfoPath)) {
    if (Test-Path -LiteralPath $destination) { throw "Refusing to overwrite existing package output: $destination" }
}
$stagingPath = Join-Path $outputPath ('.staging-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stagingPath | Out-Null
try {
    Copy-Item -LiteralPath $exe -Destination $stagingPath
    Copy-Item -LiteralPath (Join-Path $inputPath 'Resources') -Destination $stagingPath -Recurse
    Copy-Item -LiteralPath $projectLicenseSource -Destination $stagingPath -Recurse
    $licenseDestination = Join-Path $stagingPath 'LICENSES/KamataEngine'
    New-Item -ItemType Directory -Path $licenseDestination -Force | Out-Null
    Get-ChildItem -LiteralPath $licenseSource -Force | Copy-Item -Destination $licenseDestination -Recurse
    Assert-ResourceManifestsMatch $deployedResources (Get-ResourceManifest (Join-Path $stagingPath 'Resources'))
    $utf8 = New-Object System.Text.UTF8Encoding($false)
    $metadataJson = ($packageInfo | ConvertTo-Json -Depth 12) + "`n"
    [IO.File]::WriteAllText((Join-Path $stagingPath 'build-info.json'), $metadataJson, $utf8)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::CreateFromDirectory($stagingPath, $zipPath, [IO.Compression.CompressionLevel]::Optimal, $false)
    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText($checksumPath, "$zipHash  $baseName.zip`n", $utf8)
    [IO.File]::WriteAllText($externalInfoPath, $metadataJson, $utf8)
    Write-Host "Package created: $zipPath"
    Write-Host "SHA-256: $zipHash"
    if ($buildInfo.sourceDirty -or $sourceDirty -or $buildInfo.commit -ne $sourceCommit) {
        Write-Warning 'Local development package: linked EXE and current resource source may differ; inspect build-info.json. Not an official release.'
    }
}
finally {
    $resolvedStaging = [IO.Path]::GetFullPath($stagingPath)
    if ($resolvedStaging.StartsWith($outputPath.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -and
        [IO.Path]::GetFileName($resolvedStaging) -match '^\.staging-[a-f0-9]{32}$') {
        Remove-Item -LiteralPath $resolvedStaging -Recurse -Force
    }
}
