; SPDX-FileCopyrightText: 2026 Sonulab
; SPDX-License-Identifier: GPL-3.0-only

#define AppName "Sonulab StompPRO USB Driver"
#define AppVersion "0.3.0"
#define AppPublisher "Sonulab"
#define DriverFile "SonulabStompProDriver.dll"

[Setup]
AppId={{5E943643-71D2-43A5-A115-6F4FC42A92D5}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisher={#AppPublisher}
DefaultDirName={autopf}\Sonulab\StompPRO USB Driver
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64
MinVersion=10.0.17763
OutputDir=..\dist
OutputBaseFilename=Sonulab-StompPRO-USB-Driver-{#AppVersion}-Setup
Compression=lzma2/ultra64
SolidCompression=yes
WizardStyle=modern
LicenseFile=..\LICENSE
UninstallDisplayName={#AppName}
SetupLogging=yes
CloseApplications=yes
RestartApplications=no
VersionInfoVersion=0.3.0.0
VersionInfoCompany={#AppPublisher}
VersionInfoDescription={#AppName} installer
VersionInfoProductName={#AppName}
VersionInfoProductVersion={#AppVersion}
VersionInfoCopyright=Copyright (C) 2026 Sonulab

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"

[Files]
Source: "..\build\installer-stage\x64\{#DriverFile}"; DestDir: "{app}\x64"; Flags: ignoreversion regserver 64bit restartreplace uninsrestartdelete
Source: "..\build\installer-stage\x86\{#DriverFile}"; DestDir: "{app}\x86"; Flags: ignoreversion regserver 32bit restartreplace uninsrestartdelete
Source: "..\build\installer-stage\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\installer-stage\DEVELOPMENT.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\installer-stage\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\installer-stage\THIRD_PARTY_NOTICES.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\installer-stage\SOURCE.txt"; DestDir: "{app}"; Flags: ignoreversion
