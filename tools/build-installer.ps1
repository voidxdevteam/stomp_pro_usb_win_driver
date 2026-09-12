# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'

$version = '0.3.0'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$stage = Join-Path $build 'installer-stage'
$dist = Join-Path $root 'dist'
$script = Join-Path $root 'installer\SonulabStompProUsbDriver.iss'
$x64Dll = Join-Path $build 'x64\Release\SonulabStompProDriver.dll'
$x86Dll = Join-Path $build 'x86\Release\SonulabStompProDriver.dll'
$output = Join-Path $dist "Sonulab-StompPRO-USB-Driver-$version-Setup.exe"

if (-not $SkipBuild) {
    & (Join-Path $PSScriptRoot 'build.ps1')
}

$changes = & git -C $root status --porcelain
if ($LASTEXITCODE -ne 0 -or $changes) {
    throw 'Create the installer only from a clean Git commit.'
}
if (-not (Test-Path -LiteralPath $x64Dll) -or -not (Test-Path -LiteralPath $x86Dll)) {
    throw 'Both x64 and x86 release driver DLLs are required.'
}

$isccCandidates = @(
    (Get-Command 'ISCC.exe' -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -ErrorAction SilentlyContinue),
    'C:\Program Files (x86)\Inno Setup 6\ISCC.exe',
    'C:\Program Files\Inno Setup 6\ISCC.exe'
) | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -Unique
$iscc = $isccCandidates | Select-Object -First 1
if (-not $iscc) {
    throw 'Inno Setup 6 compiler (ISCC.exe) was not found.'
}

if (Test-Path -LiteralPath $stage) {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path.TrimEnd('\')
    if ($resolvedStage -ne [System.IO.Path]::GetFullPath($stage).TrimEnd('\')) {
        throw 'Unexpected installer staging path.'
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
New-Item -ItemType Directory -Path (Join-Path $stage 'x64') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $stage 'x86') -Force | Out-Null
New-Item -ItemType Directory -Path $dist -Force | Out-Null

Copy-Item -LiteralPath $x64Dll -Destination (Join-Path $stage 'x64')
Copy-Item -LiteralPath $x86Dll -Destination (Join-Path $stage 'x86')
Copy-Item -LiteralPath (Join-Path $root 'README.md'),(Join-Path $root 'DEVELOPMENT.md'),(Join-Path $root 'LICENSE'),(Join-Path $root 'THIRD_PARTY_NOTICES.md') -Destination $stage
$commit = (& git -C $root rev-parse HEAD).Trim()
@(
    "Corresponding source: Sonulab-StompPRO-USB-Driver-$version-source.zip"
    "Git commit: $commit"
    'License: GNU GPL version 3 only'
) | Set-Content -LiteralPath (Join-Path $stage 'SOURCE.txt')

& $iscc '/Qp' $script
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $output)) {
    throw 'Installer compilation failed.'
}

Remove-Item -LiteralPath $stage -Recurse -Force
Write-Host "Installer written to $output"
