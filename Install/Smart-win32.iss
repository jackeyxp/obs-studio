; �ű��� Inno Setup �ű������ɡ�
; �����ĵ���ȡ���� INNO SETUP �ű��ļ���ϸ����!

[Setup]
AppName={reg:HKLM\Software\HaoYiSmart, AppName|˫ʦ�ն�}
AppVerName={reg:HKLM\Software\HaoYiSmart, AppVerName|˫ʦ�ն�}
AppPublisher={reg:HKLM\Software\HaoYiSmart, AppPublisher|������һ�Ƽ����޹�˾}
AppPublisherURL={reg:HKLM\Software\HaoYiSmart, AppPublisherURL|https://myhaoyi.com}

DefaultGroupName={reg:HKLM\Software\HaoYiSmart, DefaultGroupName|˫ʦ�ն�}
DefaultDirName={reg:HKLM\Software\HaoYiSmart, DefaultDirName|{pf}\˫ʦ�ն�}

Compression=lzma
SolidCompression=yes
UsePreviousAppDir=no
UsePreviousGroup=noUsePreviousLanguage=no
AllowCancelDuringInstall=no
OutputDir=..\Product

VersionInfoVersion=2.0.2
OutputBaseFilename=smart-win32-2.0.2

[Languages]
Name: "chinese"; MessagesFile: "compiler:Default.isl"

[Registry]
Root: HKLM; Subkey: "Software\HaoYiSmart"; ValueType: string; ValueName: "DefaultDirName"; ValueData: "{app}"; Flags: uninsdeletekey

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}";

[Files]
Source: "..\vsbuild\rundir\Release\*.*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs

[Icons]
Name: "{userdesktop}\˫ʦ�ն�"; Filename: "{app}\bin\32bit\Smart.exe"; Tasks: desktopicon
Name: "{group}\{cm:LaunchProgram,˫ʦ�ն�}"; Filename: "{app}\bin\32bit\Smart.exe"
Name: "{group}\{cm:UninstallProgram,˫ʦ�ն�}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\bin\32bit\Smart.exe"; Description: "{cm:LaunchProgram,˫ʦ�ն�}"; Flags: nowait postinstall skipifsilent

