[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][ValidateSet('vs2026', 'ninja')][string]$Generator,
    [Parameter(Mandatory = $true)][ValidateSet('Debug', 'Release')][string]$Configuration
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildTools.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$logPath = Join-Path $repoRoot "target/ci-logs/$Generator-$Configuration"
New-Item -ItemType Directory -Path $logPath -Force | Out-Null
Start-Transcript -Path (Join-Path $logPath 'build.log') -Force | Out-Null
Push-Location -LiteralPath $repoRoot
try {
    $cmake = Initialize-CiToolchain
    $ctest = Join-Path ([IO.Path]::GetDirectoryName($cmake)) 'ctest.exe'
    Invoke-CheckedNative $cmake @('--version')
    Invoke-CheckedNative $ctest @('--version')
    Invoke-CheckedNative 'ninja.exe' @('--version')
    Write-Host "MSVC: $env:VCToolsVersion; Windows SDK: $env:WindowsSDKVersion; arch: $env:VSCMD_ARG_TGT_ARCH"
    $configurePreset = 'vs2026-x64'
    $buildPreset = $Configuration.ToLowerInvariant()
    if ($Generator -eq 'ninja') {
        $configurePreset = 'clion-' + $Configuration.ToLowerInvariant()
        $buildPreset = $configurePreset
    }
    Invoke-CheckedNative $cmake @('--preset', $configurePreset, '-DBUILD_TESTING=ON')
    if ($Generator -eq 'vs2026') {
        # /MP still parallelizes C++ compilation. Avoid nested/reused MSBuild workers.
        Invoke-CheckedNative $cmake @('--build', '--preset', $buildPreset, '--parallel', '1', '--', '/nr:false')
    } else {
        Invoke-CheckedNative $cmake @('--build', '--preset', $buildPreset, '--parallel', '4')
    }
    Invoke-CheckedNative $ctest @('--test-dir', "build/$configurePreset", '-C', $Configuration,
        '--output-on-failure', '--no-tests=error', '--output-junit', (Join-Path $logPath 'ctest.xml'))
    & (Join-Path $PSScriptRoot 'Test-PackageSupport.ps1')
    if (-not $?) { throw 'Package script checks failed.' }
}
finally {
    Pop-Location
    Stop-Transcript | Out-Null
}
