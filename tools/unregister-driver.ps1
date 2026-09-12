# SPDX-FileCopyrightText: 2026 Sonulab
# SPDX-License-Identifier: GPL-3.0-only

$ErrorActionPreference = 'Stop'

function Test-DriverRegistration([Microsoft.Win32.RegistryView]$view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $asio = $base.OpenSubKey('SOFTWARE\ASIO\Sonulab StompPRO USB Driver')
        $com = $base.OpenSubKey(
            'SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}')
        try {
            return $null -ne $asio -or $null -ne $com
        } finally {
            if ($asio) { $asio.Dispose() }
            if ($com) { $com.Dispose() }
        }
    } finally {
        $base.Dispose()
    }
}

function Remove-DriverRegistration([Microsoft.Win32.RegistryView]$view) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    try {
        $base.DeleteSubKeyTree('SOFTWARE\ASIO\Sonulab StompPRO USB Driver', $false)
        $base.DeleteSubKeyTree('SOFTWARE\ASIO\Sonulab ASIO', $false)
        $base.DeleteSubKeyTree(
            'SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}', $false)
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
    if ((Test-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry64)) -or
        (Test-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry32))) {
        throw 'Windows did not unregister both Sonulab StompPRO USB Driver architectures.'
    }
    Write-Host 'Sonulab StompPRO USB Driver unregistered for x64 and x86 hosts.'
    exit 0
}

$installDirectory = Join-Path $env:ProgramFiles 'Sonulab\StompPRO USB Driver'
$installedX64 = Join-Path $installDirectory 'x64\SonulabStompProDriver.dll'
$installedX86 = Join-Path $installDirectory 'x86\SonulabStompProDriver.dll'
$legacyDll = Join-Path $installDirectory 'SonulabStompProDriver.dll'
if (Test-Path -LiteralPath $installedX64) {
    & "$env:SystemRoot\System32\regsvr32.exe" /s /u $installedX64
}
if (Test-Path -LiteralPath $installedX86) {
    & "$env:SystemRoot\SysWOW64\regsvr32.exe" /s /u $installedX86
}
if (Test-Path -LiteralPath $legacyDll) {
    & "$env:SystemRoot\System32\regsvr32.exe" /s /u $legacyDll
}

Remove-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry64)
Remove-DriverRegistration ([Microsoft.Win32.RegistryView]::Registry32)
Remove-Item -LiteralPath $installedX64,$installedX86,$legacyDll -Force -ErrorAction SilentlyContinue
foreach ($directory in @((Split-Path $installedX64), (Split-Path $installedX86))) {
    if (Test-Path -LiteralPath $directory) {
        Remove-Item -LiteralPath $directory -Force -ErrorAction SilentlyContinue
    }
}
Write-Host 'Sonulab StompPRO USB Driver unregistered for x64 and x86 hosts.'
