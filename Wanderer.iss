; Compile through the adjacent Build-Installer.cmd. Payload is an existing UAT Windows archive.
#ifndef ReleaseDir
  #error ReleaseDir is required
#endif
#ifndef ReleaseVersion
  #error ReleaseVersion is required
#endif
#ifndef OutputDir
  #error OutputDir is required
#endif

[Setup]
AppId={{AFDBA414-AC2A-4988-A936-6C67D967658B}
AppName=Wanderer Audio Demo
AppVersion={#ReleaseVersion}
DefaultDirName={localappdata}\Programs\WandererAudioDemo
DefaultGroupName=Wanderer Audio Demo
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir={#OutputDir}
OutputBaseFilename=Wanderer-{#ReleaseVersion}-Setup
UninstallDisplayIcon={app}\Wanderer.exe
Compression=lzma2/fast
SolidCompression=no
WizardStyle=modern
CloseApplications=yes
#ifdef SingleExe
DiskSpanning=no
#else
DiskSpanning=yes
DiskSliceSize=2000000000
SlicesPerDisk=1
#endif

[Files]
Source: "{#ReleaseDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Wanderer Audio Demo"; Filename: "{app}\Wanderer.exe"; WorkingDir: "{app}"
Name: "{group}\Uninstall Wanderer Audio Demo"; Filename: "{uninstallexe}"

[Run]
; The UE prerequisite installer requests elevation itself when required.
Filename: "{app}\Engine\Extras\Redist\en-us\UEPrereqSetup_x64.exe"; Parameters: "/quiet /norestart"; StatusMsg: "Installing Unreal prerequisites..."; Flags: shellexec waituntilterminated skipifdoesntexist; Check: not WizardSilent
Filename: "{app}\Wanderer.exe"; Description: "Launch Wanderer Audio Demo"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent
