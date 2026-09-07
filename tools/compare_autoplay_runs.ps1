# Compare simulation output, not rendered pixels or wall-clock durations.
[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$ReferenceRoot,
    [Parameter(Mandatory)] [string]$CandidateRoot
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$referencePath = (Resolve-Path -LiteralPath (Join-Path $repoRoot $ReferenceRoot)).Path
$candidatePath = (Resolve-Path -LiteralPath (Join-Path $repoRoot $CandidateRoot)).Path
$files = @('summary.txt', 'frames.csv', 'enemies.csv', 'objects.csv', 'events.csv')

function Get-CompletedScenarios([string]$Root) {
    $directories = @(Get-ChildItem -LiteralPath $Root -Directory | Sort-Object Name)
    $manifestPath = Join-Path $Root 'suite-manifest.json'
    if (Test-Path -LiteralPath $manifestPath) {
        $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
        if ($manifest.Status -ne 'Complete' -or $manifest.FailedScenarios -ne 0 -or
            $manifest.CompletedScenarios -ne $manifest.ExpectedScenarios.Count) {
            throw "Suite is not complete and passing: $Root"
        }
        if (Compare-Object @($directories.Name) @($manifest.ExpectedScenarios)) {
            throw "Scenario directories do not match the manifest: $Root"
        }
    }
    if ($directories.Count -eq 0) { throw "No scenarios found: $Root" }
    foreach ($directory in $directories) {
        $summary = @{}
        foreach ($line in Get-Content -LiteralPath (Join-Path $directory.FullName 'summary.txt')) {
            $parts = $line.Split('=', 2)
            if ($parts.Count -eq 2) { $summary[$parts[0]] = $parts[1] }
        }
        if ($summary.application_succeeded -ne '1' -or $summary.harness_complete -ne '1' -or
            [int]$summary.total_steps -le 0 -or $summary.completed_steps -ne $summary.total_steps) {
            throw "Scenario is not complete and passing: $($directory.FullName)"
        }
    }
    return $directories.Name
}

$referenceScenarios = @(Get-CompletedScenarios $referencePath)
$candidateScenarios = @(Get-CompletedScenarios $candidatePath)
if (Compare-Object $referenceScenarios $candidateScenarios) {
    throw 'The runs contain different scenario sets.'
}
$differences = @(
    foreach ($scenario in $referenceScenarios) {
        foreach ($file in $files) {
            $referenceFile = Join-Path (Join-Path $referencePath $scenario) $file
            $candidateFile = Join-Path (Join-Path $candidatePath $scenario) $file
            if ((Get-FileHash -LiteralPath $referenceFile -Algorithm SHA256).Hash -ne
                (Get-FileHash -LiteralPath $candidateFile -Algorithm SHA256).Hash) {
                [pscustomobject]@{ Scenario = $scenario; File = $file }
            }
        }
    }
)
[pscustomobject]@{
    Reference = $referencePath
    Candidate = $candidatePath
    ComparedScenarios = $referenceScenarios.Count
    ComparedFiles = $referenceScenarios.Count * $files.Count
    DifferentFiles = $differences.Count
    Passed = $differences.Count -eq 0
}
if ($differences.Count -ne 0) {
    $differences
    throw 'Autoplay simulation outputs differ; inspect the reported scenarios and files.'
}
