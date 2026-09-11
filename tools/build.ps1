# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
cmake --build $build --config Release

Write-Host "Built: $build\Release\SonulabStompProDriver.dll"
