[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [string]$KamataEngineRoot = "",

    [switch]$SkipBuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if (-not $SkipBuild) {
    $buildArguments = @("-Configuration", $Configuration)
    if ($KamataEngineRoot) {
        $buildArguments += @("-KamataEngineRoot", $KamataEngineRoot)
    }

    & (Join-Path $PSScriptRoot "Build.ps1") @buildArguments
}

$executablePath = Join-Path `
    $PSScriptRoot `
    "target\$Configuration\Object_Connect.exe"

if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
    throw "Executable not found: $executablePath. Run Build.ps1 first."
}

$workingDirectory = Split-Path -Parent $executablePath
$process = Start-Process `
    -FilePath $executablePath `
    -WorkingDirectory $workingDirectory `
    -PassThru `
    -Wait

if ($process.ExitCode -ne 0) {
    throw "Object_Connect exited with code $($process.ExitCode)."
}
