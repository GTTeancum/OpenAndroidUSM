# Small synthetic log fixtures; no game processes or original assets are used.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$fixtureRoot = 'analysis\generated\autoplay-comparison-unit-' + [Guid]::NewGuid().ToString('N')

foreach ($case in @('equal', 'different-log', 'different-scenarios', 'failed-summary', 'running-manifest')) {
    $reference = Join-Path $fixtureRoot (Join-Path $case 'reference')
    $candidate = Join-Path $fixtureRoot (Join-Path $case 'candidate')
    foreach ($side in @($reference, $candidate)) {
        $scenarioName = if ($case -eq 'different-scenarios' -and $side -eq $candidate) { 'other' } else { 'sample' }
        $scenarioPath = Join-Path $repoRoot (Join-Path $side $scenarioName)
        [System.IO.Directory]::CreateDirectory($scenarioPath) | Out-Null
        $success = [int](-not ($case -eq 'failed-summary' -and $side -eq $candidate))
        @("application_succeeded=$success", 'harness_complete=1', 'completed_steps=2', 'total_steps=2') |
            Set-Content -LiteralPath (Join-Path $scenarioPath 'summary.txt') -Encoding utf8
        foreach ($file in @('frames.csv', 'enemies.csv', 'objects.csv', 'events.csv')) {
            @('sample,value', '1,2') | Set-Content -LiteralPath (Join-Path $scenarioPath $file) -Encoding utf8
        }
        if ($side -eq $candidate) {
            if ($case -eq 'different-log') {
                '2,3' | Add-Content -LiteralPath (Join-Path $scenarioPath 'frames.csv') -Encoding utf8
            }
            if ($case -eq 'running-manifest') {
                '{"Status":"Running","FailedScenarios":0,"CompletedScenarios":0,"ExpectedScenarios":["sample"]}' |
                    Set-Content -LiteralPath (Join-Path $repoRoot (Join-Path $side 'suite-manifest.json')) -Encoding utf8
            }
        }
    }
    $caught = ''
    try {
        $result = & (Join-Path $repoRoot 'tools\compare_autoplay_runs.ps1') `
            -ReferenceRoot $reference -CandidateRoot $candidate
    } catch {
        $caught = $_.Exception.Message
    }
    $expected = switch ($case) {
        'different-log' { 'Autoplay simulation outputs differ;*' }
        'different-scenarios' { 'The runs contain different scenario sets.' }
        'failed-summary' { 'Scenario is not complete and passing:*' }
        'running-manifest' { 'Suite is not complete and passing:*' }
        default { '' }
    }
    if ($caught -notlike $expected) { throw "Comparison test failed: $case ($caught)" }
    if ($case -eq 'equal' -and (-not $result.Passed -or $result.ComparedFiles -ne 5 -or $result.DifferentFiles -ne 0)) {
        throw 'Equal comparison fixture produced an incorrect result'
    }
    [pscustomobject]@{ Case = $case; Passed = $true }
}
