[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repositoryRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $PSScriptRoot '..'))
$sourcePath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'assets\ChromeTaskbarMerger.svg'))
$outputPath = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'assets\ChromeTaskbarMerger.ico'))
$buildDirectory = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'build-icon'))
$masterPath = Join-Path $buildDirectory 'icon-master.png'

function Assert-PathWithinRepository {
    param([Parameter(Mandatory)][string]$Path)

    $prefix = $repositoryRoot.TrimEnd('\') + '\'
    if (-not $Path.StartsWith(
            $prefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to modify a path outside the repository: $Path"
    }
}

Assert-PathWithinRepository -Path $outputPath
Assert-PathWithinRepository -Path $buildDirectory

$chromeCandidates = @(
    "$env:ProgramFiles\Google\Chrome\Application\chrome.exe",
    "${env:ProgramFiles(x86)}\Google\Chrome\Application\chrome.exe"
)
$chrome = $chromeCandidates |
    Where-Object { $_ -and (Test-Path -LiteralPath $_ -PathType Leaf) } |
    Select-Object -First 1
if (-not $chrome) {
    throw 'Google Chrome is required to rasterize the SVG icon.'
}

$ffmpeg = Get-Command ffmpeg -ErrorAction Stop

if (Test-Path -LiteralPath $buildDirectory) {
    Remove-Item -LiteralPath $buildDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $buildDirectory | Out-Null

$sourceUri = [System.Uri]::new($sourcePath).AbsoluteUri
$profilePath = Join-Path $buildDirectory 'chrome-profile'
$chromeArguments = @(
    '--headless=new',
    '--disable-gpu',
    '--hide-scrollbars',
    '--force-device-scale-factor=1',
    '--window-size=256,256',
    '--default-background-color=00000000',
    "--user-data-dir=$profilePath",
    "--screenshot=$masterPath",
    $sourceUri
)
$chromeProcess = Start-Process `
    -FilePath $chrome `
    -ArgumentList $chromeArguments `
    -Wait `
    -PassThru `
    -WindowStyle Hidden
if ($chromeProcess.ExitCode -ne 0 -or
    -not (Test-Path -LiteralPath $masterPath -PathType Leaf)) {
    throw 'Chrome failed to rasterize the SVG icon.'
}

$sizes = @(16, 20, 24, 32, 40, 48, 64, 128, 256)
$images = foreach ($size in $sizes) {
    $path = Join-Path $buildDirectory "icon-$size.png"
    & $ffmpeg.Source `
        -hide_banner -loglevel error -y `
        -i $masterPath `
        -vf "scale=${size}:${size}:flags=lanczos,format=rgba" `
        -frames:v 1 `
        $path
    if ($LASTEXITCODE -ne 0) {
        throw "FFmpeg failed to render the ${size}px icon."
    }
    [PSCustomObject]@{
        Size = $size
        Data = [System.IO.File]::ReadAllBytes($path)
    }
}

$stream = [System.IO.File]::Open(
    $outputPath,
    [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::None)
$writer = [System.IO.BinaryWriter]::new($stream)
try {
    $writer.Write([UInt16]0)
    $writer.Write([UInt16]1)
    $writer.Write([UInt16]$images.Count)

    [UInt32]$offset = 6 + (16 * $images.Count)
    foreach ($image in $images) {
        $dimension = if ($image.Size -eq 256) {
            [byte]0
        } else {
            [byte]$image.Size
        }
        $writer.Write($dimension)
        $writer.Write($dimension)
        $writer.Write([byte]0)
        $writer.Write([byte]0)
        $writer.Write([UInt16]1)
        $writer.Write([UInt16]32)
        $writer.Write([UInt32]$image.Data.Length)
        $writer.Write($offset)
        $offset += [UInt32]$image.Data.Length
    }
    foreach ($image in $images) {
        $writer.Write($image.Data)
    }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}

Write-Host "Icon source: $sourcePath"
Write-Host "Icon output: $outputPath"
