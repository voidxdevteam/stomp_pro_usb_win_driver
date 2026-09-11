$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\Release\SonulabASIO.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Driver DLL not found: $dll. Run tools\build.ps1 first."
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Run this script from an elevated PowerShell session.'
}

& "$env:SystemRoot\System32\regsvr32.exe" /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "regsvr32 failed with exit code $LASTEXITCODE"
}
Write-Host 'Sonulab ASIO registered.'

