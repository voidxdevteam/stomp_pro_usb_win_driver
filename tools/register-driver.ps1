# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$packagedDll = Join-Path $root 'SonulabStompProDriver.dll'
$buildDll = Join-Path $root 'build\Release\SonulabStompProDriver.dll'
$sourceDll = if (Test-Path -LiteralPath $packagedDll) { $packagedDll } else { $buildDll }
$installDirectory = Join-Path $env:ProgramFiles 'Sonulab\StompPRO USB Driver'
$installedDll = Join-Path $installDirectory 'SonulabStompProDriver.dll'
$asioKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Sonulab StompPRO USB Driver'
$comKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}\InprocServer32'

if (-not (Test-Path -LiteralPath $sourceDll)) {
    throw 'Driver DLL not found. Build the project or use the binary release package.'
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait -WindowStyle Hidden
    if (-not (Test-Path -LiteralPath $asioKey) -or -not (Test-Path -LiteralPath $comKey)) {
        throw 'Windows did not register Sonulab StompPRO USB Driver.'
    }
    Write-Host 'Sonulab StompPRO USB Driver registered.'
    exit 0
}

New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
Copy-Item -LiteralPath $sourceDll -Destination $installedDll -Force
& "$env:SystemRoot\System32\regsvr32.exe" /s $installedDll
if ($LASTEXITCODE -ne 0) {
    throw "regsvr32 failed with exit code $LASTEXITCODE"
}
Write-Host "Sonulab StompPRO USB Driver installed in $installDirectory"

