# Test reporting and stale-output rejection without launching the game.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixtureRelative = 'analysis\generated\autoplay-runner-unit-' + [Guid]::NewGuid().ToString('N')
$fakeBuildRelative = Join-Path $fixtureRelative 'build'
$fakeExecutable = Join-Path $repoRoot (Join-Path $fakeBuildRelative 'Release\OpenAndroidUSM.exe')
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $fakeExecutable)) | Out-Null
$runnerTestState = @{ Mode = ''; Launches = 0; ScriptHashCalls = 0 }

function Get-FileHash {
    param([string]$LiteralPath, [string]$Algorithm)
    $hash = Microsoft.PowerShell.Utility\Get-FileHash -LiteralPath $LiteralPath -Algorithm $Algorithm
    if ($runnerTestState.Mode -eq 'changed-script' -and $LiteralPath.EndsWith('.usmauto')) {
        ++$runnerTestState.ScriptHashCalls
        if ($runnerTestState.ScriptHashCalls -gt 1) {
            # Simulate a changed file without modifying a real scenario.
            $hash.Hash = '0' * 64
        }
    }
    return $hash
}

# This shadows the process-launch cmdlet only inside this test script and its
# child scopes. The fixture executable is deliberately not runnable.
function Start-Process {
    param(
        [string]$FilePath,
        [string[]]$ArgumentList,
        [string]$WindowStyle,
        [switch]$Wait,
        [switch]$PassThru
    )
    ++$runnerTestState.Launches
    if ($FilePath -ne $fakeExecutable -or $WindowStyle -ne 'Hidden' -or
        -not $Wait -or -not $PassThru) {
        throw 'Unexpected process invocation in runner test'
    }
    $outputIndex = [Array]::IndexOf($ArgumentList, '--output')
    if ($outputIndex -lt 0) { throw 'Runner omitted output argument' }
    $outputPath = $ArgumentList[$outputIndex + 1]
    $summaryPath = Join-Path $outputPath 'summary.txt'
    $mode = $runnerTestState.Mode
    if ($mode -ne 'missing-summary') {
        $applicationSucceeded = [int]($mode -ne 'application-failed')
        $harnessComplete = [int]($mode -ne 'harness-incomplete')
        $completed = if ($mode -eq 'step-mismatch') { 1 } else { 2 }
        $total = 2
        if ($mode -eq 'empty-script') { $completed = 0; $total = 0 }
        @(
            "application_succeeded=$applicationSucceeded"
            "harness_complete=$harnessComplete"
            "completed_steps=$completed"
            "total_steps=$total"
            'detail=runner unit-test fixture'
        ) | Set-Content -LiteralPath $summaryPath -Encoding utf8
        if ($mode -eq 'stale-summary') {
            (Get-Item -LiteralPath $summaryPath).LastWriteTimeUtc = [DateTime]::UtcNow.AddHours(-1)
        }
    }
    if ($mode -eq 'changed-executable') {
        [System.IO.File]::AppendAllText($FilePath, ' changed fixture')
    }
    # A placeholder tests capture cleanup; it is not a rendered game image.
    [System.IO.File]::WriteAllBytes((Join-Path $outputPath 'fixture.bmp'), [byte[]]@(0, 1))
    [pscustomobject]@{ ExitCode = $(if ($mode -eq 'nonzero-exit') { 7 } else { 0 }) }
}

$cases = @(
    'success', 'application-failed', 'harness-incomplete', 'step-mismatch',
    'empty-script', 'nonzero-exit', 'missing-summary', 'stale-summary',
    'changed-executable', 'changed-script', 'keep-captures'
)
foreach ($case in $cases) {
    [System.IO.File]::WriteAllText($fakeExecutable, 'Runner test fixture, not an executable.')
    $runnerTestState.Mode = $case
    $runnerTestState.ScriptHashCalls = 0
    $outputRelative = Join-Path $fixtureRelative $case
    $caughtFailure = $false
    try {
        & (Join-Path $repoRoot 'tools\run_autoplay_suite.ps1') `
            -BuildDirectory $fakeBuildRelative -OutputRoot $outputRelative `
            -Scenario comic-cover-probe -KeepCaptures:($case -eq 'keep-captures') | Out-Null
    } catch {
        if ($_.Exception.Message -ne 'One or more autoplay scenarios failed.') { throw }
        $caughtFailure = $true
    }
    $expectedPass = $case -in @('success', 'keep-captures')
    $expectedCaptures = [int]($case -ne 'keep-captures')
    $outputPath = Join-Path $repoRoot $outputRelative
    $manifest = Get-Content -LiteralPath (Join-Path $outputPath 'suite-manifest.json') -Raw |
        ConvertFrom-Json
    $results = @(Import-Csv -LiteralPath (Join-Path $outputPath 'suite-results.csv'))
    if ($caughtFailure -eq $expectedPass -or $results.Count -ne 1 -or
        ($results[0].Passed -eq 'True') -ne $expectedPass -or
        $manifest.Status -ne $(if ($expectedPass) { 'Complete' } else { 'Failed' }) -or
        $manifest.CompletedScenarios -ne 1 -or
        $manifest.PassedScenarios -ne [int]$expectedPass -or
        $manifest.FailedScenarios -ne [int](-not $expectedPass) -or
        [int]$results[0].CapturesCleaned -ne $expectedCaptures -or
        (Test-Path -LiteralPath (Join-Path $outputPath 'comic-cover-probe\fixture.bmp')) -ne
            ($case -eq 'keep-captures')) {
        throw "Runner test failed: $case"
    }
    if ($manifest.ExecutableSha256.Length -ne 64 -or $results[0].ScriptSha256.Length -ne 64) {
        throw "Runner test omitted provenance: $case"
    }
    [pscustomobject]@{ Case = $case; Passed = $true }
}

$launchesBefore = $runnerTestState.Launches
$unknownRejected = $false
try {
    & (Join-Path $repoRoot 'tools\run_autoplay_suite.ps1') `
        -BuildDirectory $fakeBuildRelative -OutputRoot (Join-Path $fixtureRelative 'unknown') `
        -Scenario not-a-real-scenario | Out-Null
} catch {
    if ($_.Exception.Message -ne 'Unknown autoplay scenario: not-a-real-scenario') { throw }
    $unknownRejected = $true
}
if (-not $unknownRejected -or $runnerTestState.Launches -ne $launchesBefore) {
    throw 'Unknown scenario was not rejected before launching'
}
[pscustomobject]@{ Case = 'unknown-scenario'; Passed = $true }
