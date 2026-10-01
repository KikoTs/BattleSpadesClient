; BattleSpades Windows installer (Inno Setup 6).
; Build with installer\build-installer.ps1, which stages a CMake install tree
; plus the bundled server and runs ISCC with:
;   /DAppVersion=0.2.0-beta.1 /DNumericVersion=0.2.0.0 /DStageDir=<stage>\bin
;
; What it does
;   * Installs per user (no UAC) into <Steam library>\steamapps\common\aceofspades\BattleSpades,
;     a subfolder of the retail game, so no original file is touched. Without
;     Ace of Spades installed it defaults to %LOCALAPPDATA%\Programs\BattleSpades.
;   * Imports the retail assets from the Ace of Spades folder (they are never
;     redistributed) with BattleSpadesAssetInstaller.exe.
;   * Optionally points Steam's Ace of Spades launch options at
;     BattleSpadesLauncher.exe and/or adds a non-Steam shortcut, both through
;     BattleSpadesSetupHelper.exe, which backs up every Steam file first and
;     refuses to write while Steam is running. Uninstall restores them.
; See docs/INSTALLER_AND_UPDATER.md.

#ifndef AppVersion
  #error Pass /DAppVersion=<release version>
#endif
#ifndef StageDir
  #error Pass /DStageDir=<CMake install prefix>\bin
#endif
#ifndef NumericVersion
  #define NumericVersion "0.0.0.0"
#endif

#define AppName "BattleSpades"
#define LauncherExe "BattleSpadesLauncher.exe"
#define HelperExe "BattleSpadesSetupHelper.exe"

[Setup]
AppId={{6C1F7B2E-5B53-4D0B-9E0E-7B8C2F4A9D31}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=BattleSpades
AppPublisherURL=https://github.com/KikoTs/BattleSpadesClient
AppSupportURL=https://github.com/KikoTs/BattleSpadesClient/issues
AppUpdatesURL=https://github.com/KikoTs/BattleSpadesClient/releases
VersionInfoVersion={#NumericVersion}
VersionInfoProductVersion={#NumericVersion}
VersionInfoProductTextVersion={#AppVersion}
DefaultDirName={code:DefaultInstallDir}
UsePreviousAppDir=yes
DirExistsWarning=no
DisableProgramGroupPage=yes
; Per-user: Steam's folder is user-writable by default, and the auto-updater
; must be able to write the install folder without elevation later on.
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
SetupIconFile=..\..\src\platform\windows\game.ico
UninstallDisplayIcon={app}\{#LauncherExe}
UninstallDisplayName={#AppName}
LicenseFile={#StageDir}\LICENSE
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=BattleSpades-Setup-{#AppVersion}
CloseApplications=yes
RestartApplications=no

[Tasks]
Name: "steamlaunch"; Description: "Start BattleSpades when I press Play on Ace of Spades in Steam (Steam overlay, friends and playtime show Ace of Spades)"; GroupDescription: "Steam:"; Check: GameDetected
Name: "steamshortcut"; Description: "Also add BattleSpades to my Steam library as a non-Steam game"; GroupDescription: "Steam:"; Flags: unchecked; Check: SteamDetected
Name: "importassets"; Description: "Import the original game files from my Ace of Spades installation now"; GroupDescription: "Game files:"; Check: GameDetected
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\ui-layout.json"; Flags: ignoreversion recursesubdirs createallsubdirs
; A player-edited HUD layout survives reinstalls (same rule as the updater's "preserve").
Source: "{#StageDir}\ui-layout.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
; Extracted to {tmp} before the wizard runs, for Steam/game detection.
Source: "{#StageDir}\{#HelperExe}"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"
Name: "{autoprograms}\{#AppName} - restore previous version"; Filename: "{app}\{#LauncherExe}"; Parameters: "--rollback"; WorkingDir: "{app}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#LauncherExe}"; Description: "Play {#AppName}"; Flags: postinstall nowait skipifsilent unchecked

[InstallDelete]
; A (re)install sets every component to the installer's versions again;
; drop what the launcher recorded for earlier component updates.
Type: files; Name: "{app}\update\installed.json"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\update"

[Code]
var
  SteamRoot: String;
  GameDir: String;
  TasksPageSeen: Boolean;

function DetectedValue(const Lines: TArrayOfString; const Key: String): String;
var
  I: Integer;
begin
  Result := '';
  for I := 0 to GetArrayLength(Lines) - 1 do
    if Pos(Key + '=', Lines[I]) = 1 then
    begin
      Result := Copy(Lines[I], Length(Key) + 2, MaxInt);
      Exit;
    end;
end;

{ Runs the helper; its key=value report lands in Lines. Returns the exit code, -1 if it could not start. }
function RunHelper(const Helper, Params: String; var Lines: TArrayOfString): Integer;
var
  OutFile: String;
  Code: Integer;
begin
  OutFile := ExpandConstant('{tmp}\helper-') + IntToStr(Random(1000000)) + '.txt';
  SetArrayLength(Lines, 0);
  if not Exec(Helper, Params + ' --out "' + OutFile + '"', '', SW_HIDE, ewWaitUntilTerminated, Code) then
  begin
    Result := -1;
    Exit;
  end;
  LoadStringsFromFile(OutFile, Lines);
  DeleteFile(OutFile);
  Result := Code;
end;

function InitializeSetup(): Boolean;
var
  Lines: TArrayOfString;
begin
  ExtractTemporaryFile('{#HelperExe}');
  RunHelper(ExpandConstant('{tmp}\{#HelperExe}'), 'detect', Lines);
  SteamRoot := DetectedValue(Lines, 'steam_root');
  GameDir := DetectedValue(Lines, 'game_dir');
  Log('Steam root: ' + SteamRoot + '; Ace of Spades: ' + GameDir);
  Result := True;
end;

function SteamDetected(): Boolean;
begin
  Result := SteamRoot <> '';
end;

function GameDetected(): Boolean;
begin
  Result := GameDir <> '';
end;

function DefaultInstallDir(Param: String): String;
begin
  if GameDir <> '' then
    Result := AddBackslash(GameDir) + '{#AppName}'
  else
    Result := ExpandConstant('{localappdata}\Programs\{#AppName}');
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  { Without Ace of Spades there are no launch options to set: offer the shortcut instead. }
  if (CurPageID = wpSelectTasks) and not TasksPageSeen then
  begin
    TasksPageSeen := True;
    if SteamDetected() and not GameDetected() then
      WizardSelectTasks('steamshortcut');
  end;
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Dir: String;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    Dir := RemoveBackslash(WizardDirValue());
    if FileExists(AddBackslash(Dir) + 'aos.exe') or
       ((GameDir <> '') and (CompareText(Dir, RemoveBackslash(GameDir)) = 0)) then
    begin
      MsgBox('This is the Ace of Spades folder itself. Choose a subfolder such as' + #13#10 +
             AddBackslash(Dir) + '{#AppName}' + #13#10 +
             'so that no original game file is replaced.', mbError, MB_OK);
      Result := False;
    end;
  end;
end;

procedure ImportAssets();
var
  Code: Integer;
begin
  WizardForm.StatusLabel.Caption := 'Importing the original Ace of Spades files...';
  if not Exec(ExpandConstant('{app}\BattleSpadesAssetInstaller.exe'), '--source "' + GameDir + '"', ExpandConstant('{app}'),
              SW_HIDE, ewWaitUntilTerminated, Code) or (Code <> 0) then
    SuppressibleMsgBox('The original game files could not be imported automatically (code ' + IntToStr(Code) + ').' + #13#10 +
                       'BattleSpades will ask for your Ace of Spades folder the first time it starts.',
                       mbInformation, MB_OK, IDOK);
end;

procedure RegisterWithSteam();
var
  Params: String;
  Lines: TArrayOfString;
  Code: Integer;
begin
  Params := 'register --launcher "' + ExpandConstant('{app}\{#LauncherExe}') + '"';
  if WizardIsTaskSelected('steamlaunch') then Params := Params + ' --launch-options';
  if WizardIsTaskSelected('steamshortcut') then Params := Params + ' --shortcut';
  WizardForm.StatusLabel.Caption := 'Registering BattleSpades with Steam...';
  repeat
    Code := RunHelper(ExpandConstant('{app}\{#HelperExe}'), Params, Lines);
    Log('Steam registration exit ' + IntToStr(Code));
    if Code = 2 then
    begin
      if WizardSilent() or
         (MsgBox('Steam is running. It rewrites its settings when it exits, so it must be closed first.' + #13#10#13#10 +
                 'Exit Steam completely (Steam menu > Exit, and wait for its tray icon to disappear), then click Retry.' + #13#10 +
                 'Cancel skips this step; see the BattleSpades documentation to register later.',
                 mbError, MB_RETRYCANCEL) <> IDRETRY) then
        Exit;
    end
    else if Code <> 0 then
    begin
      SuppressibleMsgBox('BattleSpades could not be registered with Steam:' + #13#10 +
                         DetectedValue(Lines, 'error') + DetectedValue(Lines, 'account.' + DetectedValue(Lines, 'account') + '.launch_options_error'),
                         mbError, MB_OK, IDOK);
      Exit;
    end;
  until Code <> 2;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsTaskSelected('importassets') then ImportAssets();
    if WizardIsTaskSelected('steamlaunch') or WizardIsTaskSelected('steamshortcut') then RegisterWithSteam();
  end;
end;

{ ---------------------------------------------------------------- uninstall }

procedure UnregisterFromSteam();
var
  Lines: TArrayOfString;
  Code: Integer;
begin
  repeat
    Code := RunHelper(ExpandConstant('{app}\{#HelperExe}'), 'unregister', Lines);
    Log('Steam unregistration exit ' + IntToStr(Code));
    if Code = 2 then
    begin
      if UninstallSilent() or
         (MsgBox('Steam is running. Exit Steam completely, then click Retry so its Ace of Spades launch options can be restored.' + #13#10 +
                 'If you cancel, clear them yourself: Steam > Ace of Spades > Properties > Launch options.',
                 mbError, MB_RETRYCANCEL) <> IDRETRY) then
        Exit;
    end;
  until Code <> 2;
end;

procedure DeletePlayerData(const App: String);
begin
  DeleteFile(App + '\settings.toml');
  DeleteFile(App + '\server_region.txt');
  DeleteFile(App + '\server_favorites.txt');
  DeleteFile(App + '\class_loadouts.json');
  DeleteFile(App + '\skin-variants.json');
  DeleteFile(App + '\steam_appid.txt');
  DelTree(App + '\*.log', False, True, False);
  DelTree(App + '\local-server-sessions', True, True, True);
  DelTree(App + '\hosted_ugc', True, True, True);
  DelTree(App + '\steam', True, True, True);
  DelTree(App + '\assets\original', True, True, True);
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  App: String;
begin
  App := ExpandConstant('{app}');
  if CurUninstallStep = usUninstall then
    UnregisterFromSteam();
  if CurUninstallStep = usPostUninstall then
  begin
    { Only named files: a player may have installed into a folder holding other things. Screenshots are always kept. }
    if not UninstallSilent() and
       (MsgBox('Also delete your BattleSpades settings, logs, hosted maps and the imported copy of the original game files?',
               mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES) then
      DeletePlayerData(App);
    RemoveDir(App + '\assets');
    RemoveDir(App);
  end;
end;
