; 脚本用 Inno Setup 脚本向导生成。
; 查阅文档获取创建 INNO SETUP 脚本文件详细资料!

[Setup]
AppName={reg:HKLM\Software\HaoYiSmart, AppName|双师终端}
AppVerName={reg:HKLM\Software\HaoYiSmart, AppVerName|双师终端}
AppPublisher={reg:HKLM\Software\HaoYiSmart, AppPublisher|北京浩一科技有限公司}
AppPublisherURL={reg:HKLM\Software\HaoYiSmart, AppPublisherURL|https://myhaoyi.com}

DefaultGroupName={reg:HKLM\Software\HaoYiSmart, DefaultGroupName|双师终端}
DefaultDirName={reg:HKLM\Software\HaoYiSmart, DefaultDirName|{pf}\双师终端}

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
Name: "{userdesktop}\双师终端"; Filename: "{app}\bin\32bit\Smart.exe"; Tasks: desktopicon
Name: "{group}\{cm:LaunchProgram,双师终端}"; Filename: "{app}\bin\32bit\Smart.exe"
Name: "{group}\{cm:UninstallProgram,双师终端}"; Filename: "{uninstallexe}"

[Run]
Filename: "{app}\bin\32bit\Smart.exe"; Description: "{cm:LaunchProgram,双师终端}"; Flags: nowait postinstall skipifsilent

