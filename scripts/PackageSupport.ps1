Set-StrictMode -Version Latest

function Get-ResourceManifest {
    param([Parameter(Mandatory = $true)][string]$Directory)
    $root = (Resolve-Path -LiteralPath $Directory).ProviderPath.TrimEnd('\', '/')
    $files = @(Get-ChildItem -LiteralPath $root -File -Recurse -Force | Sort-Object FullName)
    if ($files.Count -eq 0) { throw "Resource directory is empty: $root" }
    $manifest = [ordered]@{}
    foreach ($file in $files) {
        if (($file.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw "Resource links are not allowed: $($file.FullName)" }
        $relative = $file.FullName.Substring($root.Length + 1).Replace('\', '/')
        $manifest[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return $manifest
}

function Assert-ResourceManifestsMatch {
    param([System.Collections.IDictionary]$Expected, [System.Collections.IDictionary]$Actual)
    if ($Expected.Count -ne $Actual.Count) { throw "Resource count mismatch: source=$($Expected.Count), deployed=$($Actual.Count). Rebuild Release." }
    foreach ($name in $Expected.Keys) {
        if (-not $Actual.Contains($name) -or $Expected[$name] -ne $Actual[$name]) {
            throw "Resource missing or modified: $name. Rebuild Release."
        }
    }
}

function Assert-ReleaseDependencies {
    param([Parameter(Mandatory = $true)][string]$DumpbinOutput)
    $dependencies = @([regex]::Matches($DumpbinOutput, '(?im)^\s*([a-z0-9_.-]+\.dll)\s*$') |
        ForEach-Object { $_.Groups[1].Value.ToLowerInvariant() } | Sort-Object -Unique)
    if ($dependencies.Count -eq 0) { throw 'dumpbin did not report DLL dependencies; refusing an unverified package.' }
    $forbidden = @($dependencies | Where-Object {
        $_ -match '^(msvcp|msvcr|vcruntime|vcomp|concrt|libomp|libiomp)' -or
        $_ -match '^(ucrtbased|dxcompiler|dxil)\.dll$' -or
        $_ -match '^api-ms-win-crt-'
    })
    if ($forbidden.Count -gt 0) { throw "Release depends on forbidden runtime DLLs: $($forbidden -join ', ')" }
    return $dependencies
}
