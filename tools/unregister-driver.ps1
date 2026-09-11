# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

$installDirectory = Join-Path $env:ProgramFiles 'Sonulab\StompPRO USB Driver'
$installedDll = Join-Path $installDirectory 'SonulabStompProDriver.dll'
$asioKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Sonulab StompPRO USB Driver'
$legacyAsioKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Sonulab ASIO'
$comClassKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}'

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait -WindowStyle Hidden
    if ((Test-Path -LiteralPath $asioKey) -or
        (Test-Path -LiteralPath $legacyAsioKey) -or
        (Test-Path -LiteralPath $comClassKey)) {
        throw 'Windows did not unregister Sonulab StompPRO USB Driver.'
    }
    Write-Host 'Sonulab StompPRO USB Driver unregistered.'
    exit 0
}

if (Test-Path -LiteralPath $installedDll) {
    & "$env:SystemRoot\System32\regsvr32.exe" /s /u $installedDll
    if ($LASTEXITCODE -ne 0) {
        throw "regsvr32 failed with exit code $LASTEXITCODE"
    }
}
Remove-Item -LiteralPath $asioKey -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $legacyAsioKey -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $comClassKey -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $installedDll -Force -ErrorAction SilentlyContinue
if (Test-Path -LiteralPath $installDirectory) {
    Remove-Item -LiteralPath $installDirectory -Force -ErrorAction SilentlyContinue
}
Write-Host 'Sonulab StompPRO USB Driver unregistered.'
