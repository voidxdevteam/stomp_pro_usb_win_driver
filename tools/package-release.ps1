# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

$version = '0.3.1'
$root = Split-Path -Parent $PSScriptRoot
$dist = Join-Path $root 'dist'
$binaryName = "Sonulab-StompPRO-USB-Driver-$version-win64"
$sourceName = "Sonulab-StompPRO-USB-Driver-$version-source"
$binaryStage = Join-Path $dist $binaryName
$binaryZip = Join-Path $dist "$binaryName.zip"
$sourceZip = Join-Path $dist "$sourceName.zip"
$installer = Join-Path $dist "Sonulab-StompPRO-USB-Driver-$version-Setup.exe"

& (Join-Path $PSScriptRoot 'build.ps1')

$changes = & git -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $changes) {
    throw 'Create release packages only from a clean Git commit.'
}

if (Test-Path -LiteralPath $dist) {
    $resolvedDist = (Resolve-Path -LiteralPath $dist).Path.TrimEnd('\')
    if ($resolvedDist -ne [System.IO.Path]::GetFullPath($dist).TrimEnd('\')) {
        throw 'Unexpected release output path.'
    }
    Remove-Item -LiteralPath $resolvedDist -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $binaryStage 'x64') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $binaryStage 'x86') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $binaryStage 'tools') -Force | Out-Null

Copy-Item -LiteralPath (Join-Path $root 'build\x64\Release\SonulabStompProDriver.dll') -Destination (Join-Path $binaryStage 'x64')
Copy-Item -LiteralPath (Join-Path $root 'build\x64\Release\sonulab_asio_smoke.exe') -Destination (Join-Path $binaryStage 'x64')
Copy-Item -LiteralPath (Join-Path $root 'build\x86\Release\SonulabStompProDriver.dll') -Destination (Join-Path $binaryStage 'x86')
Copy-Item -LiteralPath (Join-Path $root 'build\x86\Release\sonulab_asio_smoke.exe') -Destination (Join-Path $binaryStage 'x86')
Copy-Item -LiteralPath (Join-Path $root 'README.md'),(Join-Path $root 'DEVELOPMENT.md'),(Join-Path $root 'LICENSE'),(Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $binaryStage
Copy-Item -LiteralPath (Join-Path $root 'tools\register-driver.ps1'),(Join-Path $root 'tools\unregister-driver.ps1') -Destination (Join-Path $binaryStage 'tools')

$commit = (& git -C $root rev-parse HEAD).Trim()
@(
    "Corresponding source: $sourceName.zip"
    "Git commit: $commit"
    'License: GNU GPL version 3 only'
) | Set-Content -LiteralPath (Join-Path $binaryStage 'SOURCE.txt')

Compress-Archive -Path (Join-Path $binaryStage '*') -DestinationPath $binaryZip -CompressionLevel Optimal
& git -C $root archive --format=zip --output=$sourceZip --prefix="$sourceName/" HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Unable to create source archive.'
}

& (Join-Path $PSScriptRoot 'build-installer.ps1') -SkipBuild

$hashLines = foreach ($file in @($installer, $binaryZip, $sourceZip)) {
    $hash = Get-FileHash -Algorithm SHA256 -LiteralPath $file
    "$($hash.Hash.ToLowerInvariant())  $([System.IO.Path]::GetFileName($file))"
}
$hashLines | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt')
Remove-Item -LiteralPath $binaryStage -Recurse -Force

Write-Host "Release packages written to $dist"
