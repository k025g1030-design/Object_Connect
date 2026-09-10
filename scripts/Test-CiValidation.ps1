#requires -Version 7.0
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$workflow = Get-Content -LiteralPath (Join-Path $repoRoot '.github/workflows/windows-build.yml') -Raw
$jobsStart = [regex]::Match($workflow, '(?m)^jobs:\r?$')
if (-not $jobsStart.Success) { throw 'Workflow jobs section is missing.' }
$jobsText = $workflow.Substring($jobsStart.Index)
$script:assertions = 0
function Assert-CiTest {
    param([bool]$Condition, [string]$Name)
    if (-not $Condition) { throw "CI validation assertion failed: $Name" }
    $script:assertions++
}
function Get-WorkflowJobText {
    param([string]$Job)
    $match = [regex]::Match($jobsText, '(?ms)^  ' + [regex]::Escape($Job) + ':\r?\n(?<job>.*?)(?=^  [a-z_]+:\r?$|\z)')
    if (-not $match.Success) { throw "Workflow job is missing: $Job" }
    return $match.Groups['job'].Value
}

# Test the actual inline workflow gate, not a second implementation that could
# drift away from the status check configured in GitHub branch protection.
$validation = Get-WorkflowJobText 'validation'
$run = [regex]::Match($validation, '(?ms)^        run: \|\r?\n(?<body>(?:^          [^\r\n]*\r?\n|^\r?\n)+)')
if (-not $run.Success) { throw 'The inline CI validation gate could not be located.' }
$gate = [scriptblock]::Create([regex]::Replace($run.Groups['body'].Value, '(?m)^          ', ''))
Assert-CiTest ($validation.Contains('    if: always()')) 'aggregate runs even when upstream fails/skips'
Assert-CiTest ($validation.Contains('    name: CI validation')) 'required check has a stable unique name'
Assert-CiTest ($validation.Contains('    needs: [prepare, build, package]')) 'required check covers all gates'
foreach ($job in @('build', 'package')) {
    Assert-CiTest ((Get-WorkflowJobText $job).Contains("    if: needs.prepare.outputs.needs_build == 'true'")) "$job obeys the pre-build decision"
}
$publisher = Get-WorkflowJobText 'release'
Assert-CiTest ($publisher.Contains('    needs: [prepare, package, validation]')) 'publish cannot bypass validation'
Assert-CiTest ($publisher.Contains("    if: needs.prepare.outputs.publish == 'true' && needs.prepare.outputs.needs_build == 'true'")) 'dedup cannot publish again'
$concurrency = [regex]::Match($workflow, '(?m)^concurrency:\r?\n  group: ([^\r\n]+)\r?\n  cancel-in-progress: false\r?$')
Assert-CiTest ($concurrency.Success -and $concurrency.Groups[1].Value.Contains("format('release-workflow-{0}', github.ref_name)") -and
    $concurrency.Groups[1].Value.Contains("github.event_name == 'release'") -and
    $concurrency.Groups[1].Value.Contains("github.event_name == 'push'") -and
    $concurrency.Groups[1].Value.Contains("format('ci-run-{0}', github.run_id)")) 'same-tag whole-workflow lock preserves active runs and separates ordinary CI'
Assert-CiTest (-not $concurrency.Groups[1].Value.Contains("format('release-{0}'")) 'workflow lock cannot deadlock with publisher job lock'

$variables = @('PREPARE_RESULT', 'BUILD_RESULT', 'PACKAGE_RESULT', 'NEEDS_BUILD', 'PUBLISH_RELEASE')
$original = @{}
foreach ($name in $variables) { $original[$name] = [Environment]::GetEnvironmentVariable($name) }
try {
    foreach ($prepare in @('success', 'failure', 'cancelled', 'skipped')) {
        foreach ($build in @('success', 'failure', 'cancelled', 'skipped')) {
            foreach ($package in @('success', 'failure', 'cancelled', 'skipped')) {
                foreach ($needsBuild in @('true', 'false', '')) {
                    foreach ($publish in @('true', 'false', '')) {
                        $env:PREPARE_RESULT = $prepare
                        $env:BUILD_RESULT = $build
                        $env:PACKAGE_RESULT = $package
                        $env:NEEDS_BUILD = $needsBuild
                        $env:PUBLISH_RELEASE = $publish
                        $expected = $prepare -ceq 'success' -and $publish -cin @('true', 'false') -and (
                            ($needsBuild -ceq 'true' -and $build -ceq 'success' -and $package -ceq 'success') -or
                            ($needsBuild -ceq 'false' -and $publish -ceq 'true' -and $build -ceq 'skipped' -and $package -ceq 'skipped'))
                        $passed = $false
                        try { & $gate 6>$null; $passed = $true } catch { }
                        Assert-CiTest ($passed -eq $expected) "$prepare/$build/$package needs_build=$needsBuild publish=$publish"
                    }
                }
            }
        }
    }
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $original[$name]) }
}
Write-Host "CI validation: $script:assertions workflow/status assertions passed. No remote operations performed."
