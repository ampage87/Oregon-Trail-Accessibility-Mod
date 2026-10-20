; Oregon Trail Accessibility Mod - installer (Inno Setup 6.3 or later)
;
; The installer does not contain the game.  It downloads The Oregon Trail
; Deluxe (DOS) from the Internet Archive, or copies a folder the player
; already has, downloads DOSBox Staging, installs the mod, and prepares the
; hooked copy of the game (OTAccess.exe --prepare).
;
; Build:  iscc /DAppVersion=1.0.0 installer\OTAccessSetup.iss
; Expects build\OTAccess.exe and build\OTMOD.COM (see .github/workflows/build.yml).

#define AppName "Oregon Trail Accessibility Mod"
#ifndef AppVersion
  #define AppVersion "1.0.0"
#endif
#define RepoUrl "https://github.com/ampage87/Oregon-Trail-Accessibility-Mod"
#define GameZipUrl "https://archive.org/download/msdos_Oregon_Trail_Deluxe_The_1992/Oregon_Trail_Deluxe_The_1992.zip"
#define DosBoxUrl "https://github.com/dosbox-staging/dosbox-staging/releases/download/v0.83.0/dosbox-staging-windows-x64-v0.83.0.zip"
#define DosBoxSha256 "725b915e325a6d410ce30a10989fd492fdad07f6611fff40ff77f160478810f4"
#define ShortcutName "The Oregon Trail (Accessible)"

[Setup]
AppId={{271E2C04-2ACC-43B3-9006-B15EC593450F}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=Aaron Page
AppPublisherURL={#RepoUrl}
AppSupportURL={#RepoUrl}/issues
DefaultDirName={localappdata}\Programs\Oregon Trail Accessibility Mod
DefaultGroupName=Oregon Trail Accessibility Mod
DisableProgramGroupPage=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputDir=Output
OutputBaseFilename=OregonTrailAccessibilitySetup-{#AppVersion}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
InfoBeforeFile=notice.txt
UninstallDisplayIcon={app}\OTAccess.exe
UninstallDisplayName={#AppName}
CloseApplications=yes
SetupLogging=yes

[Tasks]
Name: "startmenu"; Description: "Add a Start menu shortcut"; GroupDescription: "Shortcuts:"
Name: "desktopicon"; Description: "Add a desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "..\build\OTAccess.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\build\OTMOD.COM"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "..\src\OTAccess.default.ini"; DestDir: "{app}"; DestName: "OTAccess.ini"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\{#ShortcutName}"; Filename: "{app}\OTAccess.exe"; WorkingDir: "{app}"; Tasks: startmenu
Name: "{group}\Read me (keys and help)"; Filename: "{win}\notepad.exe"; Parameters: """{app}\README.md"""; Tasks: startmenu
Name: "{group}\Uninstall {#AppName}"; Filename: "{uninstallexe}"; Tasks: startmenu
Name: "{autodesktop}\{#ShortcutName}"; Filename: "{app}\OTAccess.exe"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\OTAccess.exe"; Description: "Start The Oregon Trail now"; WorkingDir: "{app}"; Flags: postinstall nowait skipifsilent

[UninstallDelete]
; DOSBox and the game program files; saved games (*.GAM) and the list of
; legends are left in Game Files
Type: filesandordirs; Name: "{app}\DOSBox"
Type: filesandordirs; Name: "{app}\Logs"
Type: filesandordirs; Name: "{app}\bin"
Type: files; Name: "{app}\otaccess_dosbox.conf"
Type: files; Name: "{app}\Game Files\*.EXE"
Type: files; Name: "{app}\Game Files\*.GXL"
Type: files; Name: "{app}\Game Files\*.COM"
Type: files; Name: "{app}\Game Files\*.ADV"
Type: files; Name: "{app}\Game Files\*.AD"
Type: files; Name: "{app}\Game Files\*.PF"
Type: files; Name: "{app}\Game Files\*.BAT"
Type: files; Name: "{app}\Game Files\README"

[Code]
var
  SourcePage: TInputOptionWizardPage;
  FolderPage: TInputDirWizardPage;
  DownloadPage: TDownloadWizardPage;
  NeedGame, NeedDosBox: Boolean;

function GameDir: String;
begin
  Result := ExpandConstant('{app}\Game Files');
end;

function GameAlreadyInstalled: Boolean;
begin
  Result := FileExists(GameDir + '\OREGON.EXE') and FileExists(GameDir + '\OREGON.GXL');
end;

function OnDownloadProgress(const Url, FileName: String; const Progress, ProgressMax: Int64): Boolean;
begin
  Result := True;
end;

procedure InitializeWizard;
begin
  SourcePage := CreateInputOptionPage(wpSelectTasks,
    'The Oregon Trail game files',
    'Where should the game come from?',
    'The mod does not include the game itself. Setup can download The Oregon Trail Deluxe for DOS ' +
    'from the Internet Archive, or copy the game from a folder you already have.',
    True, False);
  SourcePage.Add('Download the game from the Internet Archive (recommended)');
  SourcePage.Add('Use game files I already have');
  SourcePage.SelectedValueIndex := 0;

  FolderPage := CreateInputDirPage(SourcePage.ID,
    'Your game files',
    'Which folder holds OREGON.EXE?',
    'Choose the folder that contains OREGON.EXE and OREGON.GXL from The Oregon Trail Deluxe for DOS, ' +
    'version 3.0, then select Next. The files are copied; the folder is not changed.',
    False, '');
  FolderPage.Add('Game folder:');

  DownloadPage := CreateDownloadPage(SetupMessage(msgWizardPreparing), SetupMessage(msgPreparingDesc), @OnDownloadProgress);
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := False;
  if (PageID = SourcePage.ID) or (PageID = FolderPage.ID) then
    Result := GameAlreadyInstalled;
  if (PageID = FolderPage.ID) and (SourcePage.SelectedValueIndex = 0) then
    Result := True;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = FolderPage.ID then
  begin
    if not FileExists(AddBackslash(FolderPage.Values[0]) + 'OREGON.EXE') then
    begin
      MsgBox('OREGON.EXE was not found in that folder. Choose the folder that contains the game.', mbError, MB_OK);
      Result := False;
    end;
  end;
  if CurPageID = wpReady then
  begin
    NeedGame := (not GameAlreadyInstalled) and (SourcePage.SelectedValueIndex = 0);
    NeedDosBox := not DirExists(ExpandConstant('{app}\DOSBox'));
    DownloadPage.Clear;
    if NeedGame then
      DownloadPage.Add('{#GameZipUrl}', 'oregon.zip', '');
    if NeedDosBox then
      DownloadPage.Add('{#DosBoxUrl}', 'dosbox.zip', '{#DosBoxSha256}');
    if NeedGame or NeedDosBox then
    begin
      DownloadPage.Show;
      try
        try
          DownloadPage.Download;
          Result := True;
        except
          if DownloadPage.AbortedByUser then
            Log('Download cancelled.')
          else
            SuppressibleMsgBox('A download failed: ' + AddPeriod(GetExceptionMessage) +
              ' Check your internet connection and run Setup again.', mbCriticalError, MB_OK, IDOK);
          Result := False;
        end;
      finally
        DownloadPage.Hide;
      end;
    end;
  end;
end;

procedure SetStatus(const S: String);
begin
  WizardForm.StatusLabel.Caption := S;
  Log(S);
end;

function Unzip(const Zip, Dest: String): Boolean;
var
  Rc: Integer;
begin
  ForceDirectories(Dest);
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -ExecutionPolicy Bypass -Command "Expand-Archive -LiteralPath ''' + Zip +
    ''' -DestinationPath ''' + Dest + ''' -Force"',
    '', SW_HIDE, ewWaitUntilTerminated, Rc) and (Rc = 0);
  if not Result then
    Log('Unzip failed for ' + Zip + ' (code ' + IntToStr(Rc) + ')');
end;

{ folder (searching subfolders) that contains Name, or '' }
function FindFileDir(const Dir, Name: String): String;
var
  FR: TFindRec;
begin
  Result := '';
  if FileExists(AddBackslash(Dir) + Name) then
  begin
    Result := Dir;
    Exit;
  end;
  if FindFirst(AddBackslash(Dir) + '*', FR) then
  begin
    try
      repeat
        if ((FR.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0) and (FR.Name <> '.') and (FR.Name <> '..') then
          Result := FindFileDir(AddBackslash(Dir) + FR.Name, Name);
      until (Result <> '') or not FindNext(FR);
    finally
      FindClose(FR);
    end;
  end;
end;

procedure CopyDir(const Src, Dst: String);
var
  FR: TFindRec;
begin
  ForceDirectories(Dst);
  if FindFirst(AddBackslash(Src) + '*', FR) then
  begin
    try
      repeat
        if (FR.Name <> '.') and (FR.Name <> '..') then
        begin
          if (FR.Attributes and FILE_ATTRIBUTE_DIRECTORY) <> 0 then
            CopyDir(AddBackslash(Src) + FR.Name, AddBackslash(Dst) + FR.Name)
          else
            FileCopy(AddBackslash(Src) + FR.Name, AddBackslash(Dst) + FR.Name, False);
        end;
      until not FindNext(FR);
    finally
      FindClose(FR);
    end;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Src, DosDir, Msg: String;
  PrepLog: AnsiString;
  Rc: Integer;
begin
  if CurStep <> ssPostInstall then
    Exit;

  { the game }
  if NeedGame then
  begin
    SetStatus('Unpacking The Oregon Trail...');
    if not Unzip(ExpandConstant('{tmp}\oregon.zip'), ExpandConstant('{tmp}\game')) then
      MsgBox('The downloaded game could not be unpacked.', mbError, MB_OK)
    else
    begin
      Src := FindFileDir(ExpandConstant('{tmp}\game'), 'OREGON.EXE');
      if Src = '' then
        MsgBox('OREGON.EXE was not found in the download from the Internet Archive. ' +
          'Run Setup again and choose "Use game files I already have".', mbError, MB_OK)
      else
        CopyDir(Src, GameDir);
    end;
  end
  else if (not GameAlreadyInstalled) and (SourcePage.SelectedValueIndex = 1) then
  begin
    SetStatus('Copying your game files...');
    CopyDir(FolderPage.Values[0], GameDir);
  end;

  { the mod's DOS helper goes next to the game }
  ForceDirectories(GameDir);
  FileCopy(ExpandConstant('{app}\bin\OTMOD.COM'), GameDir + '\OTMOD.COM', False);

  { DOSBox Staging }
  if NeedDosBox then
  begin
    SetStatus('Unpacking DOSBox Staging...');
    if not Unzip(ExpandConstant('{tmp}\dosbox.zip'), ExpandConstant('{app}\DOSBox')) then
      MsgBox('DOSBox Staging could not be unpacked.', mbError, MB_OK);
  end;
  DosDir := FindFileDir(ExpandConstant('{app}\DOSBox'), 'dosbox.exe');
  if DosDir <> '' then
    SetIniString('dosbox', 'path', AddBackslash(DosDir) + 'dosbox.exe', ExpandConstant('{app}\OTAccess.ini'));

  { create the hooked copy of the game and check everything is in place }
  SetStatus('Preparing the game...');
  if Exec(ExpandConstant('{app}\OTAccess.exe'), '--prepare', ExpandConstant('{app}'), SW_HIDE, ewWaitUntilTerminated, Rc) then
  begin
    if Rc <> 0 then
    begin
      Msg := 'Setup could not finish preparing the game.';
      if LoadStringFromFile(ExpandConstant('{app}\Logs\prepare.log'), PrepLog) then
        Msg := Msg + #13#10#13#10 + String(PrepLog);
      MsgBox(Msg, mbError, MB_OK);
    end;
  end;
end;
