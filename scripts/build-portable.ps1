[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))
$buildDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'build-portable'))
$distributionRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'dist'))
$portableDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $distributionRoot 'ChromeTaskbarMerger'))
$archivePath = [System.IO.Path]::GetFullPath(
    (Join-Path $distributionRoot 'ChromeTaskbarMerger-1.0.0-rc2-portable-x64.zip'))

function Assert-PathWithinRepository {
    param([Parameter(Mandatory)][string]$Path)

    $prefix = $repositoryRoot.TrimEnd('\') + '\'
    if (-not $Path.StartsWith(
            $prefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the repository: $Path"
    }
}

Assert-PathWithinRepository -Path $buildDirectory
Assert-PathWithinRepository -Path $distributionRoot
Assert-PathWithinRepository -Path $portableDirectory
Assert-PathWithinRepository -Path $archivePath

if (Test-Path -LiteralPath $buildDirectory) {
    Remove-Item -LiteralPath $buildDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $portableDirectory) {
    Remove-Item -LiteralPath $portableDirectory -Recurse -Force
}
if (Test-Path -LiteralPath $archivePath) {
    Remove-Item -LiteralPath $archivePath -Force
}

cmake -S $repositoryRoot -B $buildDirectory -A x64
cmake --build $buildDirectory --config Debug
cmake --build $buildDirectory --config Release
ctest --test-dir $buildDirectory -C Debug --output-on-failure
ctest --test-dir $buildDirectory -C Release --output-on-failure
cmake --install $buildDirectory --config Release --prefix $portableDirectory

$requiredFiles = @(
    'ChromeTaskbarMerger.exe',
    'ChromeTaskbarMerger.ini',
    'README.md',
    'LICENSE'
)
foreach ($file in $requiredFiles) {
    $candidate = Join-Path $portableDirectory $file
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Portable package is missing: $candidate"
    }
}

Compress-Archive -LiteralPath $portableDirectory -DestinationPath $archivePath

Write-Host "Portable directory: $portableDirectory"
Write-Host "Portable archive:   $archivePath"
