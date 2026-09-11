$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\Release\SonulabASIO.dll'
$asioKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\ASIO\Sonulab ASIO'
$comKey = 'Registry::HKEY_LOCAL_MACHINE\SOFTWARE\Classes\CLSID\{672346F8-A46B-4DC4-B9A9-8322DA98056D}\InprocServer32'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Driver DLL not found: $dll. Run tools\build.ps1 first."
}

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    Start-Process -FilePath 'powershell.exe' -ArgumentList $arguments -Verb RunAs -Wait -WindowStyle Hidden
    if (-not (Test-Path -LiteralPath $asioKey) -or -not (Test-Path -LiteralPath $comKey)) {
        throw 'Windows did not register Sonulab ASIO.'
    }
    Write-Host 'Sonulab ASIO registered.'
    exit 0
}

& "$env:SystemRoot\System32\regsvr32.exe" /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "regsvr32 failed with exit code $LASTEXITCODE"
}
Write-Host 'Sonulab ASIO registered.'

