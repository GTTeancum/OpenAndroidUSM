[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Archive,

    [string]$SevenZip = 'C:\Program Files\7-Zip\7z.exe'
)

$ErrorActionPreference = 'Stop'

$archivePath = (Resolve-Path -LiteralPath $Archive).Path
if (-not (Test-Path -LiteralPath $SevenZip -PathType Leaf)) {
    throw "7-Zip was not found at: $SevenZip"
}

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$stagingRoot = Join-Path $repoRoot 'game\staging'
$originalRoot = Join-Path $repoRoot 'game\original'
$dataRoot = Join-Path $repoRoot 'game\data'

New-Item -ItemType Directory -Force -Path $stagingRoot, $originalRoot, $dataRoot | Out-Null

& $SevenZip x $archivePath `
    'spiderman\libspiderman.so' `
    'spiderman\gameloft\*' `
    "-o$stagingRoot" -y
if ($LASTEXITCODE -ne 0) {
    throw "7-Zip extraction failed with exit code $LASTEXITCODE"
}

$sourceLibrary = Join-Path $stagingRoot 'spiderman\libspiderman.so'
$sourceData = Join-Path $stagingRoot 'spiderman\gameloft'
if (-not (Test-Path -LiteralPath $sourceLibrary -PathType Leaf)) {
    throw 'The archive does not contain spiderman\libspiderman.so.'
}
if (-not (Test-Path -LiteralPath $sourceData -PathType Container)) {
    throw 'The archive does not contain spiderman\gameloft game data.'
}

$targetLibrary = Join-Path $originalRoot 'libspiderman.so'
Copy-Item -LiteralPath $sourceLibrary -Destination $targetLibrary -Force
Copy-Item -LiteralPath $sourceData -Destination $dataRoot -Recurse -Force

$libraryHash = (Get-FileHash -LiteralPath $targetLibrary -Algorithm SHA256).Hash.ToLowerInvariant()
$manifest = [ordered]@{
    archive = $archivePath
    importedAtUtc = [DateTime]::UtcNow.ToString('o')
    library = [ordered]@{
        path = 'game/original/libspiderman.so'
        size = (Get-Item -LiteralPath $targetLibrary).Length
        sha256 = $libraryHash
    }
    gameData = 'game/data/gameloft'
}

$manifestPath = Join-Path $originalRoot 'manifest.json'
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "Imported libspiderman.so ($libraryHash)"
Write-Host "Game data: $dataRoot\gameloft"
Write-Host "Manifest: $manifestPath"

