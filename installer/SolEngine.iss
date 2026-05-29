; SolEngine Beta 0.1 – Inno Setup installer script
; Run from repo root:  C:\InnoSetup6\ISCC.exe installer\SolEngine.iss

#define AppName      "SolEngine"
#define AppVersion   "0.1.0-beta"
#define AppPublisher "mathiassol"
#define AppURL       "https://solengine.pages.dev"
#define AppExeName   "sol_editor.exe"
#define BuildDir     "..\build\out\Release"
#define ExamplesDir  "..\example_scripts"

[Setup]
AppId={{B3A7C2E1-4F5D-4A8B-9C3E-1D2F5A6B7C8D}
AppName={#AppName}
AppVersion={#AppVersion}
AppPublisherURL={#AppURL}
AppSupportURL={#AppURL}
AppUpdatesURL={#AppURL}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
AllowNoIcons=yes
MinVersion=10.0
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
OutputDir=..\out
OutputBaseFilename=SolEngine-0.1.0-beta-win64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayIcon={app}\{#AppExeName}
LicenseFile=..\LICENSE
PrivilegesRequired=admin

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Additional icons:"

[Files]
; ── Core binaries ──────────────────────────────────────────────────────────────
Source: "{#BuildDir}\sol_editor.exe";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\sol.exe";          DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\sol_engine.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\demo.dll";         DestDir: "{app}"; Flags: ignoreversion

; ── DirectX shader compiler ───────────────────────────────────────────────────
Source: "{#BuildDir}\dxcompiler.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\dxil.dll";         DestDir: "{app}"; Flags: ignoreversion

; ── Qt6 runtime DLLs ──────────────────────────────────────────────────────────
Source: "{#BuildDir}\Qt6Core.dll";      DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Gui.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Network.dll";   DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Svg.dll";       DestDir: "{app}"; Flags: ignoreversion
Source: "{#BuildDir}\Qt6Widgets.dll";   DestDir: "{app}"; Flags: ignoreversion

; ── Qt6 plugins ───────────────────────────────────────────────────────────────
Source: "{#BuildDir}\platforms\*";          DestDir: "{app}\platforms";          Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\imageformats\*";       DestDir: "{app}\imageformats";       Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\iconengines\*";        DestDir: "{app}\iconengines";        Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\styles\*";             DestDir: "{app}\styles";             Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\generic\*";            DestDir: "{app}\generic";            Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\networkinformation\*"; DestDir: "{app}\networkinformation"; Flags: ignoreversion recursesubdirs
Source: "{#BuildDir}\tls\*";               DestDir: "{app}\tls";                Flags: ignoreversion recursesubdirs

; ── Editor fonts ──────────────────────────────────────────────────────────────
Source: "{#BuildDir}\fonts\*"; DestDir: "{app}\fonts"; Flags: ignoreversion recursesubdirs

; ── Example scripts ───────────────────────────────────────────────────────────
Source: "{#ExamplesDir}\*"; DestDir: "{app}\example_scripts"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{group}\{#AppName} Editor";       Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}";    Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName} Editor"; Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Launch {#AppName} Editor"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{app}\example_scripts"
