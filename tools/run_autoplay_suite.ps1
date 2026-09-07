[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build\windows-msvc',
    [string]$OutputRoot = 'analysis\generated\autoplay-suite-current',
    [string[]]$Scenario = @(),
    [switch]$KeepCaptures
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$executable = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot (Join-Path $BuildDirectory (
        Join-Path $Configuration 'OpenAndroidUSM.exe')))).Path
$scriptRoot = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot 'tests\autoplay')).Path
$outputRootPath = Join-Path $repoRoot $OutputRoot
[System.IO.Directory]::CreateDirectory($outputRootPath) | Out-Null

$scripts = Get-ChildItem -LiteralPath $scriptRoot -File -Filter '*.usmauto' |
    Where-Object { $_.Name -ne 'asset-census.usmauto' } |
    Sort-Object Name
if ($Scenario.Count -gt 0) {
    foreach ($requested in $Scenario) {
        if ($requested -notin $scripts.BaseName) {
            throw "Unknown autoplay scenario: $requested"
        }
    }
    $scripts = @($scripts | Where-Object { $_.BaseName -in $Scenario })
}
$runStartedUtc = [DateTime]::UtcNow
$executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash
$results = [System.Collections.Generic.List[object]]::new()
$manifestPath = Join-Path $outputRootPath 'suite-manifest.json'
$resultsPath = Join-Path $outputRootPath 'suite-results.csv'
function Write-SuiteManifest([string]$RunStatus) {
    [ordered]@{
        Status = $RunStatus
        StartedUtc = $runStartedUtc.ToString('o')
        UpdatedUtc = [DateTime]::UtcNow.ToString('o')
        Executable = $executable
        ExecutableSha256 = $executableHash
        ExpectedScenarios = @($scripts.BaseName)
        CompletedScenarios = $results.Count
        PassedScenarios = @($results | Where-Object Passed).Count
        FailedScenarios = @($results | Where-Object { -not $_.Passed }).Count
    } | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $manifestPath -Encoding utf8
}
Write-SuiteManifest 'Running'
$failed = $false
foreach ($scriptFile in $scripts) {
    $scenarioStartedUtc = [DateTime]::UtcNow
    $scriptHash = (Get-FileHash -LiteralPath $scriptFile.FullName -Algorithm SHA256).Hash
    $level = 1
    if ($scriptFile.BaseName -match '^level(?<LevelNumber>\d+)-') {
        $authoredLevel = [int]$Matches['LevelNumber']
        if ($authoredLevel -lt 1 -or $authoredLevel -gt 12) {
            throw "Scenario has an invalid level prefix: $($scriptFile.Name)"
        }
        $level = $authoredLevel
    }
    $output = Join-Path $outputRootPath $scriptFile.BaseName
    [System.IO.Directory]::CreateDirectory($output) | Out-Null
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--level', [string]$level,
        '--autoplay', $scriptFile.FullName,
        '--output', $output
    ) -WindowStyle Hidden -Wait -PassThru

    $summaryPath = Join-Path $output 'summary.txt'
    $detail = 'summary.txt was not produced'
    $applicationSucceeded = $false
    $harnessComplete = $false
    $completedSteps = 0
    $totalSteps = 0
    if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
        $summary = @{}
        foreach ($line in Get-Content -LiteralPath $summaryPath) {
            $parts = $line.Split('=', 2)
            if ($parts.Count -eq 2) {
                $summary[$parts[0]] = $parts[1]
            }
        }
        $applicationSucceeded = $summary['application_succeeded'] -eq '1'
        $harnessComplete = $summary['harness_complete'] -eq '1'
        $detail = $summary['detail']
        $completedSteps = [int]$summary['completed_steps']
        $totalSteps = [int]$summary['total_steps']
    }

    $captureCount = 0
    if (-not $KeepCaptures) {
        $outputPrefix = $output.TrimEnd(
            [System.IO.Path]::DirectorySeparatorChar) +
            [System.IO.Path]::DirectorySeparatorChar
        $captures = Get-ChildItem -LiteralPath $output -File -Filter '*.bmp'
        foreach ($capture in $captures) {
            if (-not $capture.FullName.StartsWith(
                    $outputPrefix,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                throw "Capture path escaped its scenario directory: $capture"
            }
            [System.IO.File]::Delete($capture.FullName)
            ++$captureCount
        }
    }

    $passed = $process.ExitCode -eq 0 -and
              $applicationSucceeded -and $harnessComplete -and
              $totalSteps -gt 0 -and $completedSteps -eq $totalSteps
    if (Test-Path -LiteralPath $summaryPath -PathType Leaf) {
        # A very fast mocked run can observe an NTFS write timestamp a few
        # sub-milliseconds behind GetUtcNow even though the file was created
        # after launch. Preserve stale-output rejection while allowing one
        # second for filesystem/clock quantization; the stale fixture remains
        # one hour old and real game runs write after many rendered frames.
        if ((Get-Item -LiteralPath $summaryPath).LastWriteTimeUtc.AddSeconds(1) -lt
            $scenarioStartedUtc) {
            $passed = $false
            $detail = 'Scenario did not produce a fresh summary'
        }
    }
    if ((Get-FileHash -LiteralPath $scriptFile.FullName -Algorithm SHA256).Hash -ne $scriptHash) {
        $passed = $false
        $detail = 'Scenario script changed during its run'
    }
    if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash -ne $executableHash) {
        $passed = $false
        $detail = 'Executable changed during this suite run'
    }
    $failed = $failed -or -not $passed
    $scenarioResult = [pscustomobject]@{
        Scenario = $scriptFile.BaseName
        Level = $level
        Passed = $passed
        ExitCode = $process.ExitCode
        CapturesCleaned = $captureCount
        Detail = $detail
        Output = $output
        StartedUtc = $scenarioStartedUtc.ToString('o')
        DurationSeconds = ([DateTime]::UtcNow - $scenarioStartedUtc).TotalSeconds
        ScriptSha256 = $scriptHash
        CompletedSteps = $completedSteps
        TotalSteps = $totalSteps
    }
    $results.Add($scenarioResult)
    $results | Export-Csv -LiteralPath $resultsPath -NoTypeInformation -Encoding utf8
    Write-SuiteManifest 'Running'
    $scenarioResult
}

Write-SuiteManifest $(if ($failed) { 'Failed' } else { 'Complete' })
if ($failed) {
    throw 'One or more autoplay scenarios failed.'
}
