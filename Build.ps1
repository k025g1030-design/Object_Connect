[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [string]$KamataEngineRoot = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Repair-PathEnvironmentVariable {
    $pathVariables = @(
        Get-ChildItem Env: | Where-Object { $_.Name -ieq "Path" }
    )

    if ($pathVariables.Count -eq 0) {
        return
    }

    # Windows の環境変数名は大文字と小文字を区別しないが、MSBuild の .NET Framework
    # ツールタスクは "Path" と "PATH" が併存する環境を拒否する。一部のターミナルが
    # 両方を追加するため、ここで正規化する。
    $canonicalPath = (
        $pathVariables |
            Sort-Object { $_.Value.Length } -Descending |
            Select-Object -First 1
    ).Value

    foreach ($unused in $pathVariables) {
        Remove-Item Env:\PATH -ErrorAction SilentlyContinue
    }

    [System.Environment]::SetEnvironmentVariable(
        "PATH",
        $canonicalPath,
        [System.EnvironmentVariableTarget]::Process
    )
}

function Get-CompatibleCMake {
    $vsWherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

    if (Test-Path -LiteralPath $vsWherePath) {
        $installationPath = @(
            & $vsWherePath `
                -latest `
                -products "*" `
                -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath
        ) | Select-Object -First 1

        if ($installationPath) {
            $bundledCMake = Join-Path `
                $installationPath.Trim() `
                "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

            if (Test-Path -LiteralPath $bundledCMake) {
                $bundledHelp = & $bundledCMake --help 2>&1 | Out-String
                if ($bundledHelp.Contains("Visual Studio 18 2026")) {
                    return $bundledCMake
                }
            }
        }
    }

    $pathCMake = Get-Command cmake -ErrorAction SilentlyContinue
    if ($pathCMake) {
        $cmakeHelp = & $pathCMake.Source --help 2>&1 | Out-String
        if ($cmakeHelp.Contains("Visual Studio 18 2026")) {
            return $pathCMake.Source
        }
    }

    throw @"
Could not find a CMake executable that supports the "Visual Studio 18 2026" generator.
Install the C++ Desktop workload for Visual Studio 2026, or place CMake 4.3+ on PATH.
"@
}

Repair-PathEnvironmentVariable

$cmakeExecutable = Get-CompatibleCMake
$configureArguments = @("--preset", "vs2026-x64")

if ($KamataEngineRoot) {
    $resolvedEngineRoot = [System.IO.Path]::GetFullPath($KamataEngineRoot)
    $configureArguments += "-DKAMATA_ENGINE_ROOT:PATH=$resolvedEngineRoot"
}

Write-Host "Using CMake: $cmakeExecutable"
Write-Host "Configuring Object_FPS..."

Push-Location -LiteralPath $PSScriptRoot
try {
    & $cmakeExecutable @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }

    $buildPreset = $Configuration.ToLowerInvariant()
    & $cmakeExecutable --build --preset $buildPreset
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}

$executablePath = Join-Path `
    $PSScriptRoot `
    "target\$Configuration\Object_FPS.exe"

Write-Host "Build completed: $executablePath"
