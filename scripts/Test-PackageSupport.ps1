[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'PackageSupport.ps1')
$repoRoot = Split-Path -Parent $PSScriptRoot
$scriptFiles = @((Join-Path $repoRoot 'Package.ps1')) + @(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.ps1' | ForEach-Object { $_.FullName })
foreach ($file in $scriptFiles) {
    $tokens = $null
    $parseErrors = $null
    [Management.Automation.Language.Parser]::ParseFile($file, [ref]$tokens, [ref]$parseErrors) | Out-Null
    if ($parseErrors.Count -gt 0) { throw "PowerShell syntax error in ${file}: $($parseErrors -join '; ')" }
}
function Assert-Rejected {
    param([scriptblock]$Action, [string]$Name)
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    if (-not $rejected) { throw "Expected rejection: $Name" }
}
$allowed = @(Assert-ReleaseDependencies "`n    KERNEL32.dll`n    D3DCOMPILER_47.dll`n    dxgi.dll`n")
if ($allowed.Count -ne 3) { throw 'System DLL parsing failed.' }
foreach ($dll in @('MSVCP140D.dll', 'VCRUNTIME140.dll', 'VCRUNTIME140_1D.dll', 'VCOMP140D.DLL',
    'VCOMP140.dll', 'ucrtbased.dll', 'dxcompiler.dll', 'dxil.dll', 'api-ms-win-crt-runtime-l1-1-0.dll')) {
    Assert-Rejected { Assert-ReleaseDependencies "`n    $dll`n    KERNEL32.dll`n" } "forbidden DLL $dll"
}
Assert-Rejected { Assert-ReleaseDependencies 'corrupted output' } 'no DLL report'
$expected = [ordered]@{ 'a.png' = '111'; 'sub/b.wav' = '222' }
Assert-ResourceManifestsMatch $expected ([ordered]@{ 'a.png' = '111'; 'sub/b.wav' = '222' })
Assert-Rejected { Assert-ResourceManifestsMatch $expected ([ordered]@{ 'a.png' = '111' }) } 'missing resource'
Assert-Rejected { Assert-ResourceManifestsMatch $expected ([ordered]@{ 'a.png' = '333'; 'sub/b.wav' = '222' }) } 'changed resource'
Assert-Rejected { Assert-ResourceManifestsMatch $expected ([ordered]@{ 'a.png' = '111'; 'sub/c.wav' = '222' }) } 'renamed resource'
Write-Host 'Package helpers: syntax, system imports, forbidden imports, missing/changed/renamed resources passed.'
