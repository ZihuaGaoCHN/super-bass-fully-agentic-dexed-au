#define ProductName "Super Bass Fully Agentic Dexed"
#define Publisher "Super Bass Fully Agentic Dexed Contributors"

#ifndef Version
  #define Version "0.0.0"
#endif
#ifndef SourceRoot
  #error SourceRoot must name the staged package directory
#endif
#ifndef OutputDirectory
  #define OutputDirectory "."
#endif

[Setup]
AppId={{4E43D3E2-BD2F-4FD1-8652-30BE4F6A7B22}
AppName={#ProductName}
AppPublisher={#Publisher}
AppVersion={#Version}
AppVerName={#ProductName} {#Version}
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DefaultDirName={autopf}\Super Bass Fully Agentic Dexed
DefaultGroupName=Super Bass Fully Agentic Dexed
LicenseFile={#SourceRoot}\LICENSE
OutputDir={#OutputDirectory}
OutputBaseFilename=Super-Bass-Fully-Agentic-Dexed-{#Version}-windows-x64-setup
Compression=lzma2
SolidCompression=yes
PrivilegesRequired=admin
SetupLogging=yes
UninstallDisplayName=Super Bass Fully Agentic Dexed {#Version}
UninstallFilesDir={app}\Uninstall
WizardStyle=modern

[Types]
Name: "full"; Description: "VST3 and Standalone"
Name: "plugin"; Description: "VST3 only"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in"; Types: full plugin custom; Flags: fixed
Name: "standalone"; Description: "Standalone diagnostic application"; Types: full custom; Flags: checkablealone
Name: "documentation"; Description: "License and documentation"; Types: full plugin custom; Flags: fixed

[Files]
Source: "{#SourceRoot}\VST3\Super Bass Fully Agentic Dexed.vst3\*"; DestDir: "{commoncf}\VST3\Super Bass Fully Agentic Dexed.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#SourceRoot}\Standalone\Super Bass Fully Agentic Dexed.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion skipifsourcedoesntexist
Source: "{#SourceRoot}\LICENSE"; DestDir: "{app}\Documentation"; Components: documentation; Flags: ignoreversion
Source: "{#SourceRoot}\THIRD_PARTY_NOTICES.md"; DestDir: "{app}\Documentation"; Components: documentation; Flags: ignoreversion
Source: "{#SourceRoot}\README.md"; DestDir: "{app}\Documentation"; Components: documentation; Flags: ignoreversion
Source: "{#SourceRoot}\manifest.json"; DestDir: "{app}\Documentation"; Components: documentation; Flags: ignoreversion

[Icons]
Name: "{group}\Super Bass Fully Agentic Dexed"; Filename: "{app}\Super Bass Fully Agentic Dexed.exe"; Components: standalone

[InstallDelete]
Type: filesandordirs; Name: "{commoncf}\VST3\Super Bass Fully Agentic Dexed.vst3"

