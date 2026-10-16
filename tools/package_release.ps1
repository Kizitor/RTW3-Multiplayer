<#
.SYNOPSIS
Packages the release zip RTW3MP-<version>-win32.zip from dist\ and extras\.

.DESCRIPTION
Reads MP_MOD_VERSION from src\mod\common.h and writes RTW3MP-<version>-win32.zip into -OutDir with:
  RTW3MP.dll, version.dll   from dist\ (build them first with build.bat)
  RTW3MP_SecondCopy.bat     from extras\
  RTW3MP_README.txt         from extras\, with {VERSION} replaced by the version
  RTW3MP_LICENSES.txt       from extras\ (MinHook's license)
Text files go into the zip as UTF-8 without BOM, with CRLF line endings. The script stops if dist\RTW3MP.dll was
not built from this version. It changes nothing in the repository and uploads nothing; at the end it prints the
zip's entries and the SHA-256 of the zip and the two DLLs (for the release notes).

.PARAMETER OutDir
Folder for the zip (created if missing). Use a folder outside the repository.

.PARAMETER Force
Replaces a zip of the same name in -OutDir.

.EXAMPLE
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package_release.ps1 -OutDir D:\rtw3mp-release
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [switch] $Force
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$repo = Split-Path -Parent $PSScriptRoot
$header = Join-Path $repo 'src\mod\common.h'
$pattern = '^\s*#define\s+MP_MOD_VERSION\s+"([^"]+)"'
$match = Select-String -LiteralPath $header -Pattern $pattern | Select-Object -First 1
if (-not $match) { throw "MP_MOD_VERSION not found in $header" }
$version = $match.Matches[0].Groups[1].Value

$dist = Join-Path $repo 'dist'
$extras = Join-Path $repo 'extras'
$files = @(  # in the order of the earlier release zips
    @{ Name = 'RTW3MP.dll'; Path = (Join-Path $dist 'RTW3MP.dll'); Text = $false },
    @{ Name = 'RTW3MP_LICENSES.txt'; Path = (Join-Path $extras 'RTW3MP_LICENSES.txt'); Text = $true },
    @{ Name = 'RTW3MP_README.txt'; Path = (Join-Path $extras 'RTW3MP_README.txt'); Text = $true },
    @{ Name = 'RTW3MP_SecondCopy.bat'; Path = (Join-Path $extras 'RTW3MP_SecondCopy.bat'); Text = $true },
    @{ Name = 'version.dll'; Path = (Join-Path $dist 'version.dll'); Text = $false }
)
foreach ($f in $files) {
    if (-not (Test-Path -LiteralPath $f.Path -PathType Leaf)) { throw "Missing $($f.Path) (run build.bat first?)" }
}

# The DLL keeps MP_MOD_VERSION as a string literal of its own: a DLL built from another version doesn't have it.
$dllText = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $dist 'RTW3MP.dll')))
if (-not $dllText.Contains("`0$version`0")) {
    throw "dist\RTW3MP.dll was not built from version $version (src\mod\common.h): run build.bat first"
}

$outFull = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutDir)
New-Item -ItemType Directory -Force -Path $outFull | Out-Null
$zipPath = Join-Path $outFull "RTW3MP-$version-win32.zip"
if (Test-Path -LiteralPath $zipPath) {
    if (-not $Force) { throw "$zipPath exists (use -Force to replace it)" }
    Remove-Item -LiteralPath $zipPath
}

$utf8 = New-Object System.Text.UTF8Encoding($false)
$zip = [System.IO.Compression.ZipFile]::Open($zipPath, [System.IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($f in $files) {
        if ($f.Text) {
            $text = [IO.File]::ReadAllText($f.Path)
            if ($f.Name -eq 'RTW3MP_README.txt') {
                if (-not $text.Contains('{VERSION}')) { throw "$($f.Path) has no {VERSION} placeholder" }
                $text = $text.Replace('{VERSION}', $version)
            }
            $text = ($text -replace "`r`n", "`n") -replace "`n", "`r`n"
            $bytes = $utf8.GetBytes($text)
        } else {
            $bytes = [IO.File]::ReadAllBytes($f.Path)
        }
        $entry = $zip.CreateEntry($f.Name, [System.IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset](Get-Item -LiteralPath $f.Path).LastWriteTime
        $stream = $entry.Open()
        try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
    }
} finally {
    $zip.Dispose()
}

Write-Output "Packaged RTW3 Multiplayer $version -> $zipPath"
$read = [System.IO.Compression.ZipFile]::OpenRead($zipPath)
try {
    foreach ($e in $read.Entries) { Write-Output ('  {0,-24} {1,9} bytes' -f $e.FullName, $e.Length) }
} finally {
    $read.Dispose()
}
Write-Output 'SHA-256:'
foreach ($p in @($zipPath, (Join-Path $dist 'version.dll'), (Join-Path $dist 'RTW3MP.dll'))) {
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $p).Hash.ToLowerInvariant()
    Write-Output ('  {0}  {1}' -f $hash, (Split-Path -Leaf $p))
}
