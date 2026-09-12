# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

[CmdletBinding()]
param(
    [ValidateSet('All', 'x64', 'Win32')]
    [string]$Architecture = 'All'
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$buildRoot = Join-Path $root 'build'
$targets = switch ($Architecture) {
    'x64' { ,@{ CMakeArchitecture = 'x64'; Directory = 'x64' } }
    'Win32' { ,@{ CMakeArchitecture = 'Win32'; Directory = 'x86' } }
    default {
        @(
            @{ CMakeArchitecture = 'x64'; Directory = 'x64' },
            @{ CMakeArchitecture = 'Win32'; Directory = 'x86' }
        )
    }
}

foreach ($target in $targets) {
    $build = Join-Path $buildRoot $target.Directory
    cmake -S $root -B $build -G 'Visual Studio 17 2022' -A $target.CMakeArchitecture
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed for $($target.CMakeArchitecture)."
    }
    cmake --build $build --config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release build failed for $($target.CMakeArchitecture)."
    }
    Write-Host "Built $($target.CMakeArchitecture): $build\Release\SonulabStompProDriver.dll"
}
