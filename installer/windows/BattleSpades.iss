; BattleSpades Windows installer (Inno Setup 6).
; Build with installer\build-installer.ps1, which stages a CMake install tree
; plus the bundled server and runs ISCC with:
;   /DAppVersion=0.2.0-beta.1 /DNumericVersion=0.2.0.0 /DStageDir=<stage>\bin
;
; Near one-click: the only pages are "Ready to install" (one Install click)
; and "Finished" (Play BattleSpades is ticked). Everything else is decided
; automatically:
;   * Per user (no UAC) into <Steam library>\steamapps\common\aceofspades\BattleSpades,
;     a subfolder of the retail game, so no original file is touched. Without
;     Ace of Spades, or when that folder is not writable, it uses
;     %LOCALAPPDATA%\Programs\BattleSpades. /DIR="..." still overrides it.
;   * Ace of Spades found: imports its original files (they are never
;     redistributed) with BattleSpadesAssetInstaller.exe, and points Steam's
;     Ace of Spades launch options at BattleSpadesLauncher.exe (Play then
;     offers BattleSpades or the original game). Steam without Ace of Spades:
;     adds a non-Steam shortcut instead. Both go through
;     BattleSpadesSetupHelper.exe, which backs up every Steam file first.
;   * Steam running: ONE question, "Restart Steam now?". No (or a silent
;     install) leaves update\steam-register.pending and the launcher finishes
;     the registration the next time it starts while Steam is closed.
;   * Start menu + desktop shortcuts. Uninstall restores Steam's settings.
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
; One-click: no welcome, license, folder, Start-menu or tasks pages. The
; license ships in the install folder (LICENSE, THIRD_PARTY_NOTICES.md).
DisableWelcomePage=yes
DisableDirPage=yes
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
Compression=lzma2/max
SolidCompression=yes
OutputBaseFilename=BattleSpades-Setup-{#AppVersion}
; Close a running BattleSpades without asking (it holds files we replace).
CloseApplications=force
RestartApplications=no

[Messages]
ReadyLabel1=BattleSpades is ready to install.
ReadyLabel2a=Click Install to continue.

[Files]
Source: "{#StageDir}\*"; DestDir: "{app}"; Excludes: "\ui-layout.json"; Flags: ignoreversion recursesubdirs createallsubdirs
; A player-edited HUD layout survives reinstalls (same rule as the updater's "preserve").
Source: "{#StageDir}\ui-layout.json"; DestDir: "{app}"; Flags: onlyifdoesntexist
; Extracted to {tmp} before the wizard runs, for Steam/game detection.
Source: "{#StageDir}\{#HelperExe}"; Flags: dontcopy

[Icons]
Name: "{autoprograms}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"
Name: "{autoprograms}\{#AppName} - restore previous version"; Filename: "{app}\{#LauncherExe}"; Parameters: "--rollback"; WorkingDir: "{app}"
Name: "{autoprograms}\{#AppName} - choose what Steam Play starts"; Filename: "{app}\{#LauncherExe}"; Parameters: "--reset-launch-choice"; WorkingDir: "{app}"; Check: GameDetected
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#LauncherExe}"; WorkingDir: "{app}"

[Run]
Filename: "{app}\{#LauncherExe}"; Description: "Play {#AppName}"; Flags: postinstall nowait skipifsilent

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

{ True when a file can be created in the existing folder Dir. }
function CanWriteTo(const Dir: String): Boolean;
var
  Probe: String;
begin
  Result := False;
  if not DirExists(Dir) then Exit;
  Probe := AddBackslash(Dir) + '.battlespades-write-test';
  Result := SaveStringToFile(Probe, 'x', False);
  if Result then DeleteFile(Probe);
end;

function DefaultInstallDir(Param: String): String;
var
  Candidate: String;
begin
  Result := ExpandConstant('{localappdata}\Programs\{#AppName}');
  if GameDir = '' then Exit;
  Candidate := AddBackslash(GameDir) + '{#AppName}';
  { Steam grants users write access to its libraries; a locked-down one would need UAC. }
  if CanWriteTo(Candidate) or (not DirExists(Candidate) and CanWriteTo(GameDir)) then
    Result := Candidate
  else
    Log('Ace of Spades folder is not writable; installing into ' + Result);
end;

function IsGameFolder(const Dir: String): Boolean;
begin
  { aos.pkg, not aos.exe: BattleSpades installs its own aos.exe, so an upgrade must not look like the game. }
  Result := FileExists(AddBackslash(Dir) + 'aos.pkg') or
            ((GameDir <> '') and (CompareText(RemoveBackslash(Dir), RemoveBackslash(GameDir)) = 0));
end;

function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  { /DIR= pointing at the game folder itself (the folder page is hidden). }
  if (CurPageID = wpReady) and IsGameFolder(WizardDirValue()) then
  begin
    MsgBox('This is the Ace of Spades folder itself. Install into a subfolder such as' + #13#10 +
           AddBackslash(RemoveBackslash(WizardDirValue())) + '{#AppName}' + #13#10 +
           'so that no original game file is replaced.', mbError, MB_OK);
    Result := False;
  end;
end;

function ImportAssets(): Boolean;
var
  Code: Integer;
  App: String;
begin
  App := ExpandConstant('{app}');
  WizardForm.StatusLabel.Caption := 'Copying the original Ace of Spades files (about 450 MB)...';
  Result := Exec(App + '\BattleSpadesAssetInstaller.exe',
                 '--source "' + GameDir + '" --destination "' + App + '\assets\original" --report "' +
                 App + '\update\import-report.txt"', App, SW_HIDE, ewWaitUntilTerminated, Code) and (Code = 0);
  { No message box: if this fails the launcher's first-run screen explains why and offers the download. }
  Log('Asset import exit ' + IntToStr(Code));
end;

function SteamExe(): String;
begin
  Result := '';
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamExe', Result) and FileExists(Result) then Exit;
  Result := AddBackslash(SteamRoot) + 'steam.exe';
  if (SteamRoot = '') or not FileExists(Result) then Result := '';
end;

function SteamRunning(const Helper: String): Boolean;
var
  Lines: TArrayOfString;
begin
  RunHelper(Helper, 'detect', Lines);
  Result := DetectedValue(Lines, 'steam_running') = '1';
end;

{ Asks Steam to exit and waits up to 60 s. }
function ShutdownSteam(const Helper: String): Boolean;
var
  Exe: String;
  Code, I: Integer;
begin
  Result := False;
  Exe := SteamExe();
  if Exe = '' then Exit;
  Exec(Exe, '-shutdown', '', SW_HIDE, ewNoWait, Code);
  for I := 1 to 60 do
  begin
    Sleep(1000);
    if not SteamRunning(Helper) then
    begin
      Sleep(1500);  { let it finish writing its config files }
      Result := True;
      Exit;
    end;
  end;
end;

procedure StartSteam();
var
  Exe: String;
  Code: Integer;
begin
  Exe := SteamExe();
  if Exe <> '' then ExecAsOriginalUser(Exe, '', '', SW_SHOWNORMAL, ewNoWait, Code);
end;

procedure LeavePendingRegistration(const Flags: String);
begin
  ForceDirectories(ExpandConstant('{app}\update'));
  SaveStringToFile(ExpandConstant('{app}\update\steam-register.pending'), Flags, False);
  Log('Steam registration left for the launcher: ' + Flags);
end;

procedure RegisterWithSteam(const Flags: String);
var
  Helper, Params: String;
  Lines: TArrayOfString;
  Code: Integer;
  Restart: Boolean;
begin
  Helper := ExpandConstant('{app}\{#HelperExe}');
  Params := 'register --launcher "' + ExpandConstant('{app}\{#LauncherExe}') + '"' + Flags;
  WizardForm.StatusLabel.Caption := 'Setting up Steam...';
  Code := RunHelper(Helper, Params, Lines);
  Log('Steam registration exit ' + IntToStr(Code));
  if Code <> 2 then
  begin
    if Code <> 0 then Log('Steam registration failed: ' + DetectedValue(Lines, 'error'));
    Exit;
  end;
  { Steam is running and would overwrite the change on exit: one clear choice. }
  Restart := False;
  if not WizardSilent() then
    Restart := MsgBox('Steam is open. To make Play on Ace of Spades in Steam start BattleSpades, Steam has to ' +
                      'restart once.' + #13#10#13#10 + 'Restart Steam now?' + #13#10#13#10 +
                      'If you choose No, BattleSpades finishes this by itself the next time you start it ' +
                      'while Steam is closed.', mbConfirmation, MB_YESNO) = IDYES;
  if not Restart then
  begin
    LeavePendingRegistration(Flags);
    Exit;
  end;
  WizardForm.StatusLabel.Caption := 'Waiting for Steam to close...';
  if not ShutdownSteam(Helper) then
  begin
    LeavePendingRegistration(Flags);
    Exit;
  end;
  WizardForm.StatusLabel.Caption := 'Setting up Steam...';
  Code := RunHelper(Helper, Params, Lines);
  Log('Steam registration (after restart) exit ' + IntToStr(Code));
  if Code = 2 then LeavePendingRegistration(Flags);
  StartSteam();
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Flags: String;
begin
  if CurStep = ssPostInstall then
  begin
    { An upgrade keeps its verified import; the launcher repairs a missing one. }
    if GameDetected() and not DirExists(ExpandConstant('{app}\assets\original\png')) then ImportAssets();
    Flags := '';
    if GameDetected() then Flags := ' --launch-options'
    else if SteamDetected() then Flags := ' --shortcut';
    if Flags <> '' then RegisterWithSteam(Flags);
  end;
end;

{ ---------------------------------------------------------------- uninstall }

procedure UnregisterFromSteam();
var
  Helper: String;
  Lines: TArrayOfString;
  Code: Integer;
begin
  Helper := ExpandConstant('{app}\{#HelperExe}');
  Code := RunHelper(Helper, 'unregister', Lines);
  Log('Steam unregistration exit ' + IntToStr(Code));
  if Code <> 2 then Exit;
  if UninstallSilent() or
     (MsgBox('Steam is open. Restart Steam now so the Ace of Spades launch options can be restored?' + #13#10#13#10 +
             'If you choose No, clear them yourself: Steam > Ace of Spades > Properties > Launch options.',
             mbConfirmation, MB_YESNO) <> IDYES) then
    Exit;
  if ShutdownSteam(Helper) then
  begin
    Code := RunHelper(Helper, 'unregister', Lines);
    Log('Steam unregistration (after restart) exit ' + IntToStr(Code));
    StartSteam();
  end;
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
