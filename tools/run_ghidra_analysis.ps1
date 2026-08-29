[CmdletBinding()]
param(
    [string]$GhidraRoot = 'C:\Programming\ghidra_11.3.2_PUBLIC',
    [string]$Library = '',
    [switch]$Reimport
)

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if ([string]::IsNullOrWhiteSpace($Library)) {
    $Library = Join-Path $repoRoot 'game\original\libspiderman.so'
}
$libraryPath = (Resolve-Path -LiteralPath $Library).Path
$headless = Join-Path $GhidraRoot 'support\analyzeHeadless.bat'
if (-not (Test-Path -LiteralPath $headless -PathType Leaf)) {
    throw "Ghidra headless analyzer was not found at: $headless"
}

$projectRoot = Join-Path $repoRoot 'analysis\ghidra-project'
$generatedRoot = Join-Path $repoRoot 'analysis\generated'
$decompilationRoot = Join-Path $generatedRoot 'seed-decompilation'
$scriptRoot = Join-Path $repoRoot 'tools\ghidra'
New-Item -ItemType Directory -Force -Path $projectRoot, $generatedRoot, $decompilationRoot | Out-Null

$commonArguments = @(
    $projectRoot,
    'OpenAndroidUSM',
    '-scriptPath', $scriptRoot,
    '-analysisTimeoutPerFile', '3600'
)

if ($Reimport -or -not (Test-Path -LiteralPath (Join-Path $projectRoot 'OpenAndroidUSM.gpr'))) {
    $arguments = $commonArguments + @(
        '-import', $libraryPath,
        '-overwrite',
        '-postScript', 'ExportFunctionInventory.java', (Join-Path $generatedRoot 'functions.csv'),
        '-postScript', 'ExportSelectedDecompilation.java', $decompilationRoot,
            'InitWin32', 'TouchScreenWin32', 'SetVisibleVirtualControl',
            'appKeyPressed', 'appKeyReleased'
    )
}
else {
    $arguments = $commonArguments + @(
        '-process', 'libspiderman.so',
        '-postScript', 'ExportFunctionInventory.java', (Join-Path $generatedRoot 'functions.csv'),
        '-postScript', 'ExportSelectedDecompilation.java', $decompilationRoot,
            'InitWin32', 'TouchScreenWin32', 'SetVisibleVirtualControl',
            'appKeyPressed', 'appKeyReleased'
    )
}

& $headless @arguments
if ($LASTEXITCODE -ne 0) {
    throw "Ghidra headless analysis failed with exit code $LASTEXITCODE"
}

