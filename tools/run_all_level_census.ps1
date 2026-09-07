[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$BuildDirectory = 'build\windows-msvc',
    [string]$OutputRoot = 'analysis\generated\level-bootstrap-census'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$executable = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot (Join-Path $BuildDirectory (
        Join-Path $Configuration 'OpenAndroidUSM.exe')))).Path
$script = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot 'tests\autoplay\asset-census.usmauto')).Path
$outputRootPath = Join-Path $repoRoot $OutputRoot
[System.IO.Directory]::CreateDirectory($outputRootPath) | Out-Null

$failed = $false
for ($level = 1; $level -le 12; ++$level) {
    $output = Join-Path $outputRootPath ("level$level")
    [System.IO.Directory]::CreateDirectory($output) | Out-Null
    $process = Start-Process -FilePath $executable -ArgumentList @(
        '--level', [string]$level,
        '--autoplay', $script,
        '--output', $output
    ) -WindowStyle Hidden -Wait -PassThru

    $summaryPath = Join-Path $output 'summary.txt'
    $detail = 'summary.txt was not produced'
    $applicationSucceeded = $false
    $harnessComplete = $false
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
    }
    $passed = $process.ExitCode -eq 0 -and
              $applicationSucceeded -and $harnessComplete
    $failed = $failed -or -not $passed
    [pscustomobject]@{
        Level = $level
        Passed = $passed
        ExitCode = $process.ExitCode
        Detail = $detail
        Output = $output
    }
}

if ($failed) {
    throw 'One or more level bootstrap censuses failed.'
}
