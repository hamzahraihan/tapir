; Inno Setup script for tapir.
; Build tapir first: cmake --build build --config Release
; Then compile this script with ISCC.exe installer\tapir.iss
; CI passes /dAppVersion=<version from tag> to override the default below.

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif

[Setup]
AppId={{348E259F-87FC-4402-9747-9CD24F621495}
AppName=tapir
AppVersion={#AppVersion}
VersionInfoVersion={#AppVersion}
AppMutex=tapir-single-instance
DefaultDirName={autopf}\tapir
DefaultGroupName=tapir
DisableProgramGroupPage=yes
UninstallDisplayName=tapir
UninstallDisplayIcon={app}\tapir.exe
SetupIconFile=..\resources\tapir.ico
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
; Per-user install: no UAC prompt, and {autopf} becomes %LOCALAPPDATA%\Programs.
PrivilegesRequired=lowest
DirExistsWarning=no
AllowNoIcons=yes
OutputDir=Output
OutputBaseFilename=tapir-setup

[Languages]
Name: english; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: desktopicon; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "..\build\Release\tapir.exe"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\tapir"; Filename: "{app}\tapir.exe"
Name: "{autodesktop}\tapir"; Filename: "{app}\tapir.exe"; Tasks: desktopicon
