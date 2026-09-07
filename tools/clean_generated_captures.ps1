[CmdletBinding()]
param(
    [string]$Root = 'analysis\generated'
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$generatedRoot = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot 'analysis\generated')).Path
$captureRoot = (Resolve-Path -LiteralPath (
    Join-Path $repoRoot $Root)).Path
$generatedPrefix = $generatedRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
if ($captureRoot -ne $generatedRoot -and
    -not $captureRoot.StartsWith(
        $generatedPrefix,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Capture cleanup root is outside analysis/generated: $captureRoot"
}

$capturePrefix = $captureRoot.TrimEnd(
    [System.IO.Path]::DirectorySeparatorChar) +
    [System.IO.Path]::DirectorySeparatorChar
$captures = Get-ChildItem -LiteralPath $captureRoot -Recurse -File -Filter '*.bmp'
$deleted = 0
foreach ($capture in $captures) {
    if (-not $capture.FullName.StartsWith(
            $capturePrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Capture path escaped the requested cleanup root: $capture"
    }
    [System.IO.File]::Delete($capture.FullName)
    ++$deleted
}

[pscustomobject]@{
    Root = $captureRoot
    Deleted = $deleted
    Remaining = @(
        Get-ChildItem -LiteralPath $captureRoot -Recurse -File -Filter '*.bmp'
    ).Count
}
