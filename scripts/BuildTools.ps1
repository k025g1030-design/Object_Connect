Set-StrictMode -Version Latest

function Get-VisualStudioInstallation {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        throw 'Visual Studio Installer / vswhere.exe was not found.'
    }
    $installation = @(& $vswhere -latest -products '*' -version '[18.0,19.0)' `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath)
    if ($LASTEXITCODE -ne 0 -or $installation.Count -eq 0) {
        throw 'Visual Studio 2026 with the Desktop development with C++ workload is required.'
    }
    return $installation[0].Trim()
}

function Get-DumpbinExecutable {
    $available = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($available) { return $available.Source }
    $installation = Get-VisualStudioInstallation
    $toolVersion = (Get-Content -LiteralPath (Join-Path $installation 'VC/Auxiliary/Build/Microsoft.VCToolsVersion.default.txt') -Raw).Trim()
    $dumpbin = Join-Path $installation "VC/Tools/MSVC/$toolVersion/bin/Hostx64/x64/dumpbin.exe"
    if (-not (Test-Path -LiteralPath $dumpbin -PathType Leaf)) { throw "dumpbin.exe was not found: $dumpbin" }
    return $dumpbin
}

function Initialize-CiToolchain {
    # Some terminals expose both Path and PATH; the .NET Framework MSBuild tasks reject that.
    $pathEntries = @(Get-ChildItem Env: | Where-Object { $_.Name -ieq 'Path' })
    if ($pathEntries.Count -gt 0) {
        $canonicalPath = ($pathEntries | Sort-Object { $_.Value.Length } -Descending | Select-Object -First 1).Value
        foreach ($entry in $pathEntries) { Remove-Item Env:\PATH -ErrorAction SilentlyContinue }
        [Environment]::SetEnvironmentVariable('PATH', $canonicalPath, 'Process')
    }
    $installation = Get-VisualStudioInstallation
    Import-Module (Join-Path $installation 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
    Enter-VsDevShell -VsInstallPath $installation -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Host
    $cmake = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
    if (-not (Test-Path -LiteralPath $cmake)) { throw "Bundled CMake is missing: $cmake" }
    $ninjaDirectory = Join-Path $installation 'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    $env:PATH = "$([IO.Path]::GetDirectoryName($cmake));$ninjaDirectory;$env:PATH"
    # Debug tests need the non-redistributable developer runtimes, including OpenMP.
    # Add only installed x64 directories to this process; never ship these files.
    $redistRoot = Join-Path $installation 'VC/Redist/MSVC'
    $debugRuntimeDirectories = @()
    if (Test-Path -LiteralPath $redistRoot) {
        foreach ($redistVersion in @(Get-ChildItem -LiteralPath $redistRoot -Directory | Sort-Object Name -Descending)) {
            $debugRoot = Join-Path $redistVersion.FullName 'debug_nonredist/x64'
            if (Test-Path -LiteralPath $debugRoot) {
                $debugRuntimeDirectories += @(Get-ChildItem -LiteralPath $debugRoot -Directory |
                    Where-Object { $_.Name -match '\.(DebugCRT|DebugOpenMP)$' } |
                    ForEach-Object { $_.FullName })
            }
        }
    }
    if ($env:WindowsSdkDir -and $env:WindowsSDKVersion) {
        $debugUcrt = Join-Path $env:WindowsSdkDir ('bin/' + $env:WindowsSDKVersion.TrimEnd('\', '/') + '/x64/ucrt')
        if (Test-Path -LiteralPath $debugUcrt) { $debugRuntimeDirectories += $debugUcrt }
    }
    if ($debugRuntimeDirectories.Count -gt 0) { $env:PATH = ($debugRuntimeDirectories -join ';') + ';' + $env:PATH }
    $cmakeHelp = & $cmake --help | Out-String
    if ($LASTEXITCODE -ne 0 -or -not $cmakeHelp.Contains('Visual Studio 18 2026')) {
        throw 'The selected CMake does not support Visual Studio 18 2026.'
    }
    return $cmake
}

function Invoke-CheckedNative {
    param([string]$Executable, [string[]]$Arguments)
    $ErrorActionPreference = 'Stop'
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE." }
}
