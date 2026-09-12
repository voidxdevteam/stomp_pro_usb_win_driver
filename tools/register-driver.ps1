# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

function Test-DriverRegistration([Microsoft.Win32.RegistryView]$view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $asio = $base.OpenSubKey('SOFTWARE\ASIO\Sonulab StompPRO USB Driver')
        $com = $base.OpenSubKey(
            'SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}\InprocServer32')
        try {
            return $null -ne $asio -and $null -ne $com
        } finally {
            if ($asio) { $asio.Dispose() }
            if ($com) { $com.Dispose() }
        }
    } finally {
        $base.Dispose()
    }
}

if (-not [Environment]::Is64BitOperatingSystem) {
    throw 'A 64-bit edition of Windows is required.'
}

$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $powerShellDirectory = if ([Environment]::Is64BitProcess) { 'System32' } else { 'Sysnative' }
    $powerShell = Join-Path $env:SystemRoot "$powerShellDirectory\WindowsPowerShell\v1.0\powershell.exe"
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    Start-Process -FilePath $powerShell -ArgumentList $arguments -Verb RunAs -Wait -WindowStyle Hidden
    if (-not (Test-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry64)) -or
        -not (Test-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry32))) {
        throw 'Windows did not register both Sonulab StompPRO USB Driver architectures.'
    }
    Write-Host 'Sonulab StompPRO USB Driver registered for x64 and x86 hosts.'
    exit 0
}

$root = Split-Path -Parent $PSScriptRoot
$packagedX64 = Join-Path $root 'x64\SonulabStompProDriver.dll'
$packagedX86 = Join-Path $root 'x86\SonulabStompProDriver.dll'
$buildX64 = Join-Path $root 'build\x64\Release\SonulabStompProDriver.dll'
$buildX86 = Join-Path $root 'build\x86\Release\SonulabStompProDriver.dll'
$sourceX64 = if (Test-Path -LiteralPath $packagedX64) { $packagedX64 } else { $buildX64 }
$sourceX86 = if (Test-Path -LiteralPath $packagedX86) { $packagedX86 } else { $buildX86 }
if (-not (Test-Path -LiteralPath $sourceX64) -or -not (Test-Path -LiteralPath $sourceX86)) {
    throw 'Both x64 and x86 driver DLLs are required.'
}

$installDirectory = Join-Path $env:ProgramFiles 'Sonulab\StompPRO USB Driver'
$installX64 = Join-Path $installDirectory 'x64'
$installX86 = Join-Path $installDirectory 'x86'
$installedX64 = Join-Path $installX64 'SonulabStompProDriver.dll'
$installedX86 = Join-Path $installX86 'SonulabStompProDriver.dll'
New-Item -ItemType Directory -Path $installX64,$installX86 -Force | Out-Null
Copy-Item -LiteralPath $sourceX64 -Destination $installedX64 -Force
Copy-Item -LiteralPath $sourceX86 -Destination $installedX86 -Force

& "$env:SystemRoot\System32\regsvr32.exe" /s $installedX64
if ($LASTEXITCODE -ne 0) {
    throw "x64 regsvr32 failed with exit code $LASTEXITCODE"
}
& "$env:SystemRoot\SysWOW64\regsvr32.exe" /s $installedX86
if ($LASTEXITCODE -ne 0) {
    $registrationExitCode = $LASTEXITCODE
    & "$env:SystemRoot\System32\regsvr32.exe" /s /u $installedX64
    throw "x86 regsvr32 failed with exit code $registrationExitCode"
}

Write-Host "Sonulab StompPRO USB Driver installed for x64 and x86 hosts in $installDirectory"
