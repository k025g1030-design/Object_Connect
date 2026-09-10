[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration,
    [Parameter(Mandatory = $true)][string]$CompilerVersion,
    [Parameter(Mandatory = $true)][string]$Generator,
    [Parameter(Mandatory = $true)][string]$CMakeVersion,
    [AllowEmptyString()][string]$WindowsSdkVersion = ''
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$commit = $null
$sourceDirty = $null
try {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        $gitRoot = (& git -C $SourceDirectory rev-parse --show-toplevel 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or -not $gitRoot -or
            [IO.Path]::GetFullPath($gitRoot).TrimEnd('\', '/') -ine [IO.Path]::GetFullPath($SourceDirectory).TrimEnd('\', '/')) {
            throw 'The source directory is not the root of its own Git checkout.'
        }
        $candidateCommit = (& git -C $SourceDirectory rev-parse --verify HEAD 2>$null | Out-String).Trim()
        if ($LASTEXITCODE -eq 0 -and $candidateCommit -match '^[a-f0-9]{40}$') {
            $status = @(& git -C $SourceDirectory status --porcelain --untracked-files=normal 2>$null)
            if ($LASTEXITCODE -eq 0) { $commit = $candidateCommit; $sourceDirty = ($status.Count -gt 0) }
        }
    }
} catch {
    # Source archives may not contain .git, and Git is not a C++ build requirement.
    $commit = $null
    $sourceDirty = $null
}
if (-not $commit) { Write-Warning 'Git provenance is unavailable. The build is usable locally, but packaging requires rebuilding from a Git checkout.' }
if (-not $WindowsSdkVersion) { $WindowsSdkVersion = [Environment]::GetEnvironmentVariable('WindowsSDKVersion') }
if ($WindowsSdkVersion) { $WindowsSdkVersion = $WindowsSdkVersion.TrimEnd('\', '/') }
$sha256 = [Security.Cryptography.SHA256]::Create()
$exeStream = [IO.File]::OpenRead($Executable)
try { $exeHash = [BitConverter]::ToString($sha256.ComputeHash($exeStream)).Replace('-', '').ToLowerInvariant() }
finally { $exeStream.Dispose(); $sha256.Dispose() }
$metadata = [ordered]@{
    schemaVersion = 1
    configuration = $Configuration
    architecture = 'x64'
    commit = $commit
    sourceDirty = $sourceDirty
    linkedAtUtc = [DateTime]::UtcNow.ToString('o')
    executableSha256 = $exeHash
    toolchain = [ordered]@{
        compiler = 'MSVC'
        compilerVersion = $CompilerVersion
        generator = $Generator
        cmakeVersion = $CMakeVersion
        windowsSdkVersion = $WindowsSdkVersion
    }
}
$destination = Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($Executable))) 'build-info.json'
$utf8 = New-Object System.Text.UTF8Encoding($false)
[IO.File]::WriteAllText($destination, ($metadata | ConvertTo-Json -Depth 8) + "`n", $utf8)
Write-Host "Recorded link provenance: $destination ($commit, dirty=$($metadata.sourceDirty))"
