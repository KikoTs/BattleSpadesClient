<# Drives one real native window into a Protocol 168 match and validates the
   world, ACK movement, server-derived pause menu and ChangeTeam screen. #>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string] $Executable,
    [Parameter(Mandatory = $true)][string] $Endpoint,
    [Parameter(Mandatory = $true)][string] $EvidenceDirectory,
    [ValidateRange(10, 60)][int] $JoinTimeoutSeconds = 30,
    # Optional deterministic visual gate: authenticate, teleport the local
    # player to a named server-owned player, and capture its live class mesh.
    [string] $TeleportToPlayer = '',
    [string] $AdminPassword = 'changeme',
    [string] $ServerRepository = '',
    # Keep the joined world alive long enough to expose delayed mode packets
    # and clean-but-unexpected disconnects. Four seconds preserves the old
    # smoke duration; soak runs can request substantially longer.
    [ValidateRange(4, 600)][int] $WorldHoldSeconds = 4,
    # Some automated/remote desktops dim an otherwise ready accelerated HWND.
    # Keep the normal foreground threshold strict while allowing an explicit
    # lower value; the five distinct post-join captures remain the final gate.
    [ValidateRange(1, 255)][int] $ReadyBrightnessThreshold = 180,
    # Optional global-chat round trip. Uses retail's default T binding, commits
    # text through the real Unicode input path, and captures input + echo.
    [string] $ChatMessage = '',
    # Optional deterministic class card (0=Soldier .. 6=Medic). -1 preserves
    # the menu's current selection.
    [ValidateRange(-1, 6)][int] $ClassIndex = -1,
    # Optional retail number-key slots captured after join. Useful for visual
    # verification of server-provided prefab variants and placement ghosts.
    [ValidateRange(1, 10)][int[]] $PlacementSlots = @(),
    # Exercise the authoritative class-change death lifecycle after ordinary
    # movement. This captures the class menu, grave camera, delayed chase
    # transition and eventual respawn without injecting client-only state.
    [switch] $ExerciseDeathState,
    # Original servers support /kill; capture our corpse after moving away
    # from spawn, then the server-driven respawn, without a retail class swap.
    [switch] $ExerciseClassicDeath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$executablePath = (Resolve-Path -LiteralPath $Executable).Path
$evidencePath = [IO.Path]::GetFullPath($EvidenceDirectory)
[IO.Directory]::CreateDirectory($evidencePath) | Out-Null

if ($null -eq ('BattleSpades.LiveSmoke.Native' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace BattleSpades.LiveSmoke {
 [StructLayout(LayoutKind.Sequential)] public struct Rect {
  public int Left,Top,Right,Bottom;
  public int Width { get { return Right-Left; } }
  public int Height { get { return Bottom-Top; } }
 }
 [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; }
 [StructLayout(LayoutKind.Sequential)] struct KeyboardInput {
  public ushort VirtualKey,ScanCode; public uint Flags,Time; public IntPtr ExtraInfo;
 }
 [StructLayout(LayoutKind.Explicit)] struct InputUnion {
  [FieldOffset(0)] public KeyboardInput Keyboard;
  // INPUT's native union is 32 bytes on Win64 because MOUSEINPUT is larger
  // than KEYBDINPUT. Without this tail SendInput rejects sizeof(INPUT)=32
  // instead of the required 40 and no raw SDL key event is generated.
  [FieldOffset(31)] public byte NativeUnionTail;
 }
 [StructLayout(LayoutKind.Sequential)] struct Input {
  public uint Type; public InputUnion Data;
 }
 public static class Native {
  const uint Owner=4, Move=0x200, Down=0x201, Up=0x202, WheelMessage=0x20A, CloseMessage=0x10;
  const uint KeyDown=0x100, KeyUp=0x101, Character=0x102, Left=1;
  const uint NoSize=1,NoMove=2,Show=0x40;
  static readonly IntPtr TopMost=new IntPtr(-1);
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr p);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr GetWindow(IntPtr h,uint c);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from,uint to,bool attach);
  [DllImport("user32.dll")] static extern IntPtr SetFocus(IntPtr h);
  [DllImport("user32.dll")] static extern IntPtr SetActiveWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool GetClientRect(IntPtr h,out Rect r);
  [DllImport("user32.dll")] static extern bool ClientToScreen(IntPtr h,ref Point p);
  [DllImport("user32.dll")] static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] static extern void mouse_event(uint flags,uint dx,uint dy,uint data,UIntPtr extra);
  [DllImport("user32.dll")] static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] static extern bool SetWindowPos(IntPtr h,IntPtr after,int x,int y,int w,int height,uint flags);
  [DllImport("user32.dll")] static extern uint MapVirtualKey(uint c,uint t);
  [DllImport("user32.dll")] static extern void keybd_event(byte key,byte scan,uint flags,UIntPtr extra);
  [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h,uint m,IntPtr w,IntPtr l);
  [DllImport("user32.dll",SetLastError=true)] static extern uint SendInput(uint count,Input[] inputs,int size);
  [DllImport("user32.dll")] static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] static extern bool SetProcessDpiAwarenessContext(IntPtr value);
  public static void PhysicalPixels() {
   try { if(SetProcessDpiAwarenessContext(new IntPtr(-4)))return; }
   catch(EntryPointNotFoundException) {}
   SetProcessDPIAware();
  }
  static IntPtr XY(int x,int y) { return new IntPtr(unchecked((int)(((uint)(ushort)y<<16)|(ushort)x))); }
  public static IntPtr Find(uint pid) {
   IntPtr best=IntPtr.Zero; long area=-1;
   EnumWindows(delegate(IntPtr h,IntPtr p) { uint owner; Rect r;
    GetWindowThreadProcessId(h,out owner);
    if(owner==pid && IsWindowVisible(h) && GetWindow(h,Owner)==IntPtr.Zero && GetClientRect(h,out r)) {
     long a=(long)r.Width*r.Height; if(a>area){area=a;best=h;}
    } return true;
   },IntPtr.Zero); return best;
  }
  public static bool ScreenRect(IntPtr h,out Rect result) {
   Rect c; Point p=new Point(); result=new Rect();
   if(!GetClientRect(h,out c)||!ClientToScreen(h,ref p))return false;
   result.Left=p.X;result.Top=p.Y;result.Right=p.X+c.Width;result.Bottom=p.Y+c.Height;return true;
  }
  public static void Front(IntPtr h){
   SetWindowPos(h,TopMost,0,0,0,0,NoSize|NoMove|Show);
   uint pid;uint target=GetWindowThreadProcessId(h,out pid);uint caller=GetCurrentThreadId();
   bool attached=target!=0&&target!=caller&&AttachThreadInput(caller,target,true);
   try{SetForegroundWindow(h);SetActiveWindow(h);SetFocus(h);}
   finally{if(attached)AttachThreadInput(caller,target,false);}
  }
  public static bool Click(IntPtr h,int x,int y){Point p=new Point{X=x,Y=y};if(!ClientToScreen(h,ref p))return false;
   SetCursorPos(p.X,p.Y);IntPtr q=XY(x,y);PostMessage(h,Move,IntPtr.Zero,q);PostMessage(h,Down,new IntPtr(Left),q);return PostMessage(h,Up,IntPtr.Zero,q);
  }
  // SDL relative mode consumes the physical mouse stream. PostMessage remains
  // correct for frontend widgets, but it cannot prove captured gameplay input.
  public static bool PhysicalClick(IntPtr h,int x,int y){Point p=new Point{X=x,Y=y};if(!ClientToScreen(h,ref p))return false;
   Front(h);SetCursorPos(p.X,p.Y);mouse_event(2,0,0,0,UIntPtr.Zero);mouse_event(4,0,0,0,UIntPtr.Zero);return true;
  }
  public static bool Text(IntPtr h,string value){foreach(char c in value)if(!PostMessage(h,Character,new IntPtr(c),IntPtr.Zero))return false;return true;}
  public static bool UnicodeText(string value){
   foreach(char c in value){
    Input[] input=new Input[2]; input[0].Type=input[1].Type=1;
    input[0].Data.Keyboard.ScanCode=input[1].Data.Keyboard.ScanCode=c;
    input[0].Data.Keyboard.Flags=4; input[1].Data.Keyboard.Flags=6;
    if(SendInput(2,input,Marshal.SizeOf(typeof(Input)))!=2)return false;
   }
   return true;
  }
  public static bool Key(IntPtr h,uint key,bool down){
   // SDL switches gameplay to raw/relative input. WM_KEYDOWN still drives the
   // pre-spawn menus but is ignored after pointer capture, which previously
   // produced false-positive world/scoreboard/pause evidence. Send a physical
   // scan code through the foreground input stream so both modes see the same
   // event a real keyboard would generate.
   Front(h);
   uint scan=MapVirtualKey(key,0);
   if(scan==0)return false;
   keybd_event((byte)key,(byte)scan,down?0U:2U,UIntPtr.Zero);
   return true;
  }
  public static bool Wheel(IntPtr h,int delta){return PostMessage(h,WheelMessage,new IntPtr(unchecked(delta<<16)),IntPtr.Zero);}
  public static bool Close(IntPtr h){return PostMessage(h,CloseMessage,IntPtr.Zero,IntPtr.Zero);}
 }
}
'@
}

# GetClientRect and CopyFromScreen must use the same physical coordinate
# system. Without per-monitor-v2 awareness a scaled desktop samples outside
# the SDL client, making a healthy match look like an all-black failed load.
[BattleSpades.LiveSmoke.Native]::PhysicalPixels()
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

function Get-Rect([IntPtr] $Window) {
    $r = [BattleSpades.LiveSmoke.Rect]::new()
    if (-not [BattleSpades.LiveSmoke.Native]::ScreenRect($Window, [ref] $r)) {
        throw 'Could not query client rectangle.'
    }
    return $r
}
function Point([IntPtr] $Window, [double] $X, [double] $Y) {
    $r = Get-Rect $Window
    $scale = [Math]::Min($r.Width / 800.0, $r.Height / 600.0)
    return [Drawing.Point]::new(
        [int][Math]::Round(($r.Width - 800*$scale)/2 + $X*$scale),
        [int][Math]::Round(($r.Height - 600*$scale)/2 + $Y*$scale))
}
function Click([IntPtr] $Window, [double] $X, [double] $Y) {
    $p = Point $Window $X $Y
    if (-not [BattleSpades.LiveSmoke.Native]::Click($Window,$p.X,$p.Y)) {
        throw "Click failed at $X,$Y"
    }
}
function Capture([IntPtr] $Window, [string] $Name) {
    $r=Get-Rect $Window; $path=[IO.Path]::Combine($evidencePath,$Name)
    $bitmap=[Drawing.Bitmap]::new($r.Width,$r.Height)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try { $graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size); $bitmap.Save($path) }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
    return $path
}
function Sample-Brightness([IntPtr] $Window, [double] $X, [double] $Y) {
    $r=Get-Rect $Window; $p=Point $Window $X $Y
    # Capture the full accelerated client surface, then inspect a local tile.
    # Some Windows/DWM drivers return a black surface for tiny partial-screen
    # copies over bgfx swap chains even though full client capture is valid.
    $bitmap=[Drawing.Bitmap]::new($r.Width,$r.Height)
    $graphics=[Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($r.Left,$r.Top,0,0,$bitmap.Size)
        $sum=0.0
        for($x=0;$x -lt 8;$x++){for($y=0;$y -lt 8;$y++){
            $sx=[Math]::Max(0,[Math]::Min($bitmap.Width-1,$p.X-4+$x))
            $sy=[Math]::Max(0,[Math]::Min($bitmap.Height-1,$p.Y-4+$y))
            $c=$bitmap.GetPixel($sx,$sy); $sum+=($c.R+$c.G+$c.B)/3.0
        }}
        return $sum/64.0
    }
    finally { $graphics.Dispose(); $bitmap.Dispose() }
}
function Alive($Process,[string]$Stage) {
    $Process.Refresh()
    if($Process.HasExited){
        # WaitForExit materializes the redirected-process exit code and flushes
        # stdout/stderr before the failure is reported to the caller.
        $Process.WaitForExit()
        throw "Client exited during $Stage with $($Process.ExitCode)."
    }
}

$stdoutPath=[IO.Path]::Combine($evidencePath,'client.stdout.log')
$stderrPath=[IO.Path]::Combine($evidencePath,'client.stderr.log')
$process=Start-Process $executablePath -ArgumentList '--run-forever','--pace','--connect',$Endpoint -WorkingDirectory ([IO.Path]::GetDirectoryName($executablePath)) -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$window=[IntPtr]::Zero
$anchorProcess=$null
try {
    $deadline=[DateTime]::UtcNow.AddSeconds(20)
    do { Alive $process 'startup'; $window=[BattleSpades.LiveSmoke.Native]::Find([uint32]$process.Id); Start-Sleep -Milliseconds 50 } while($window -eq [IntPtr]::Zero -and [DateTime]::UtcNow -lt $deadline)
    if($window -eq [IntPtr]::Zero){throw 'Timed out waiting for the native window.'}
    [BattleSpades.LiveSmoke.Native]::Front($window)
    # bgfx creates the native swap chain asynchronously. Copying the client
    # surface continuously during those first frames can observe the transient
    # all-black HWND instead of the stable SDL surface on scaled desktops.
    Start-Sleep -Seconds 5

    # Map transfer/meshing is asynchronous. Poll the material inside the
    # loading START button: retail brightens it only after map sync and GPU
    # residency complete. A disabled/error button stays below this threshold,
    # so connection failures cannot be mistaken for a successful join.
    $readyDeadline=[DateTime]::UtcNow.AddSeconds($JoinTimeoutSeconds)
    do {
        Alive $process 'join'
        $candidate=[BattleSpades.LiveSmoke.Native]::Find([uint32]$process.Id)
        if($candidate -ne [IntPtr]::Zero){$window=$candidate}
        $startBrightness=Sample-Brightness $window 550 470
        if($startBrightness -ge $ReadyBrightnessThreshold){break}
        Start-Sleep -Seconds 1
    } while([DateTime]::UtcNow -lt $readyDeadline)
    Capture $window '00-live-loading.png' | Out-Null
    if($startBrightness -lt $ReadyBrightnessThreshold){
        throw "Live loading START never enabled (brightness $([Math]::Round($startBrightness,1)))."
    }

    # Retail's join lifecycle has three distinct gates: Loading START,
    # SelectTeam, then SelectClass. Skipping SelectTeam previously hid the
    # native client's broken initial-join state machine.
    Click $window 615 478
    Start-Sleep -Seconds 1
    Alive $process 'team selection'
    $teamSelection=Capture $window '01-live-team-selection.png'
    Click $window 190 475
    Start-Sleep -Seconds 1
    Alive $process 'class selection'
    if($ClassIndex -ge 0) {
        # SelectClass displays five recovered 107x107 cards at a 134 px
        # interval. The first card begins at x=85.
        Click $window (138 + 134*$ClassIndex) 180
        Start-Sleep -Milliseconds 250
    }
    $classSelection=Capture $window '01-live-class-selection.png'
    Click $window 660 480
    Start-Sleep -Seconds $WorldHoldSeconds
    Alive $process 'live world'
    $world=Capture $window '02-live-world.png'
    if(-not [string]::IsNullOrWhiteSpace($ChatMessage)) {
        [BattleSpades.LiveSmoke.Native]::Front($window)
        if(-not [BattleSpades.LiveSmoke.Native]::Key($window,0x54,$true) -or
           -not [BattleSpades.LiveSmoke.Native]::Key($window,0x54,$false)) {
            throw 'Could not open global chat through the physical input stream.'
        }
        Start-Sleep -Milliseconds 150
        if(-not [BattleSpades.LiveSmoke.Native]::UnicodeText($ChatMessage)) {
            throw 'Could not commit Unicode chat text.'
        }
        Start-Sleep -Milliseconds 150
        Capture $window '02-live-chat-input.png' | Out-Null
        if(-not [BattleSpades.LiveSmoke.Native]::Key($window,0x0D,$true) -or
           -not [BattleSpades.LiveSmoke.Native]::Key($window,0x0D,$false)) {
            throw 'Could not submit global chat.'
        }
        Start-Sleep -Milliseconds 400
        Alive $process 'live chat echo'
        Capture $window '02-live-chat-echo.png' | Out-Null
    }
    # Foreground activation is asynchronous. TAB was occasionally consumed
    # by the desktop focus traversal when injected in the same call that
    # activated the SDL window, leaving a false world-frame "scoreboard".
    [BattleSpades.LiveSmoke.Native]::Front($window)
    Start-Sleep -Milliseconds 150
    # Prime the raw-input focus with a harmless modifier. Windows consumes the
    # first TAB when activation itself is still settling, while an ordinary
    # modifier is delivered without changing gameplay state.
    [BattleSpades.LiveSmoke.Native]::Key($window,0x10,$true)|Out-Null
    [BattleSpades.LiveSmoke.Native]::Key($window,0x10,$false)|Out-Null
    Start-Sleep -Milliseconds 50
    if(-not [BattleSpades.LiveSmoke.Native]::Key($window,0x09,$true)){
        throw 'Could not press TAB through the physical input stream.'
    }
    Start-Sleep -Milliseconds 250
    Alive $process 'live scoreboard'
    $scoreboard=Capture $window '02-live-scoreboard.png'
    if(-not [BattleSpades.LiveSmoke.Native]::Key($window,0x09,$false)){
        throw 'Could not release TAB through the physical input stream.'
    }
    # Inventory mapping is protocol-sensitive: one wheel notch must expose the
    # real loadout/prefab strip, not dormant objective tools or mechanism ids.
    [BattleSpades.LiveSmoke.Native]::Wheel($window,-120)|Out-Null
    Start-Sleep -Milliseconds 180
    Alive $process 'live inventory'
    $inventory=Capture $window '02-live-inventory.png'
    foreach($slot in $PlacementSlots) {
        $virtualKey = if($slot -eq 10) { 0x30 } else { 0x30 + $slot }
        [BattleSpades.LiveSmoke.Native]::Key($window,[uint32]$virtualKey,$true)|Out-Null
        [BattleSpades.LiveSmoke.Native]::Key($window,[uint32]$virtualKey,$false)|Out-Null
        Start-Sleep -Milliseconds 350
        Alive $process "placement slot $slot"
        Capture $window ("02-live-placement-slot-{0}.png" -f $slot) | Out-Null
    }
    if (-not [string]::IsNullOrWhiteSpace($TeleportToPlayer)) {
        if ([string]::IsNullOrWhiteSpace($ServerRepository)) {
            throw '-ServerRepository is required with -TeleportToPlayer.'
        }
        $serverPath=(Resolve-Path -LiteralPath $ServerRepository).Path
        $anchorLog=[IO.Path]::Combine($evidencePath,'02-render-anchor.jsonl')
        $anchorError=[IO.Path]::Combine($evidencePath,'02-render-anchor.stderr.log')
        $oldAdmin=$env:BATTLESPADES_RENDER_ADMIN
        $oldTarget=$env:BATTLESPADES_RENDER_TARGET
        $oldDestination=$env:BATTLESPADES_RENDER_DESTINATION
        $oldClass=$env:BATTLESPADES_RENDER_CLASS
        $oldHold=$env:BATTLESPADES_RENDER_HOLD_SECONDS
        $oldUnbuffered=$env:PYTHONUNBUFFERED
        try {
            $env:BATTLESPADES_RENDER_ADMIN=$AdminPassword
            $env:BATTLESPADES_RENDER_TARGET=$TeleportToPlayer
            $env:BATTLESPADES_RENDER_DESTINATION='Player'
            $env:BATTLESPADES_RENDER_CLASS='1'
            $env:BATTLESPADES_RENDER_HOLD_SECONDS='8'
            $env:PYTHONUNBUFFERED='1'
            $endpointPort=([Uri]::new("udp://$Endpoint")).Port
            $anchorProcess=Start-Process py2 -ArgumentList @(
                [IO.Path]::Combine($serverPath,'testbot','run.py'),
                '--scenario','render_player_anchor','--host','127.0.0.1',
                '--port',[string]$endpointPort,'--name',$TeleportToPlayer
            ) -WorkingDirectory $serverPath -WindowStyle Hidden -PassThru `
                -RedirectStandardOutput $anchorLog -RedirectStandardError $anchorError
        }
        finally {
            $env:BATTLESPADES_RENDER_ADMIN=$oldAdmin
            $env:BATTLESPADES_RENDER_TARGET=$oldTarget
            $env:BATTLESPADES_RENDER_DESTINATION=$oldDestination
            $env:BATTLESPADES_RENDER_CLASS=$oldClass
            $env:BATTLESPADES_RENDER_HOLD_SECONDS=$oldHold
            $env:PYTHONUNBUFFERED=$oldUnbuffered
        }
        $anchorDeadline=[DateTime]::UtcNow.AddSeconds(30)
        do {
            Start-Sleep -Milliseconds 200
            $anchorProcess.Refresh()
            if ($anchorProcess.HasExited) {
                throw "Render anchor client exited early with code $($anchorProcess.ExitCode)."
            }
            $anchored=(Test-Path -LiteralPath $anchorLog -PathType Leaf) -and
                ($null -ne (Select-String -LiteralPath $anchorLog `
                    -SimpleMatch '"evt": "render_player_anchored"' `
                    -ErrorAction SilentlyContinue))
        } while(-not $anchored -and [DateTime]::UtcNow -lt $anchorDeadline)
        if(-not $anchored){
            throw 'Timed out waiting for the render anchor peer to join.'
        }
        # The command overlaps both player origins. Back away immediately so
        # the named player's real networked class mesh is in front of camera.
        [BattleSpades.LiveSmoke.Native]::Key($window,0x53,$true)|Out-Null
        Start-Sleep -Milliseconds 600
        [BattleSpades.LiveSmoke.Native]::Key($window,0x53,$false)|Out-Null
        Start-Sleep -Milliseconds 150
        Alive $process 'live player render'
        Capture $window '02-live-player-render.png' | Out-Null
    }
    [BattleSpades.LiveSmoke.Native]::Key($window,0x57,$true)|Out-Null; Start-Sleep -Seconds 2; [BattleSpades.LiveSmoke.Native]::Key($window,0x57,$false)|Out-Null
    $walk=Capture $window '03-live-after-walk.png'
    if($ExerciseClassicDeath) {
        [BattleSpades.LiveSmoke.Native]::Key($window,0x54,$true)|Out-Null
        [BattleSpades.LiveSmoke.Native]::Key($window,0x54,$false)|Out-Null
        Start-Sleep -Milliseconds 150
        if(-not [BattleSpades.LiveSmoke.Native]::UnicodeText('/kill')) { throw 'Could not enter local legacy suicide command.' }
        [BattleSpades.LiveSmoke.Native]::Key($window,0x0D,$true)|Out-Null
        [BattleSpades.LiveSmoke.Native]::Key($window,0x0D,$false)|Out-Null
        Start-Sleep -Milliseconds 500
        Alive $process 'classic corpse'
        Capture $window '03-classic-corpse-after-walk.png' | Out-Null
        Start-Sleep -Seconds 5
        Alive $process 'classic respawn'
        Capture $window '03-classic-respawn.png' | Out-Null
    }
    [BattleSpades.LiveSmoke.Native]::Key($window,0x1B,$true)|Out-Null;[BattleSpades.LiveSmoke.Native]::Key($window,0x1B,$false)|Out-Null;Start-Sleep -Milliseconds 1200
    $pause=Capture $window '04-live-pause.png'
    if($ExerciseDeathState){
        Click $window 400 258
        Start-Sleep -Milliseconds 600
        Alive $process 'class-change selection'
        $deathClass=Capture $window '05-live-death-class-selection.png'
        # Select a different visible class so the server retires the current
        # life. The confirmation packet remains the normal ChangeClass /
        # SetClassLoadout transaction used by a human.
        $currentClass = if($ClassIndex -ge 0) { $ClassIndex } else { 0 }
        $nextClass = ($currentClass + 1) % 5
        Click $window (138 + 134*$nextClass) 180
        Start-Sleep -Milliseconds 150
        Click $window 660 480
        Start-Sleep -Milliseconds 450
        Alive $process 'authoritative grave camera'
        $grave=Capture $window '06-live-death-grave.png'
        Start-Sleep -Milliseconds 1800
        Alive $process 'death chase availability'
        $chaseReady=Capture $window '07-live-death-chase-ready.png'
        # One physical mouse click asks the recovered controller to leave
        # camera 5. A posted window message cannot exercise SDL relative mode.
        if(-not [BattleSpades.LiveSmoke.Native]::PhysicalClick($window,400,300)){
            throw 'Could not inject the captured death-camera click.'
        }
        Start-Sleep -Milliseconds 350
        Alive $process 'death chase camera'
        $chase=Capture $window '08-live-death-chase.png'
        Start-Sleep -Seconds 6
        Alive $process 'class-change respawn'
        $respawn=Capture $window '09-live-death-respawn.png'
        $hashes=@($teamSelection,$classSelection,$world,$scoreboard,$inventory,
            $walk,$pause,$deathClass,$grave,$chaseReady,$chase,$respawn)|
            ForEach-Object{(Get-FileHash $_ -Algorithm SHA256).Hash}
        if(@($hashes|Select-Object -Unique).Count -lt 10){
            throw 'Death lifecycle captures did not expose enough distinct states.'
        }
    } else {
        Click $window 400 323; Start-Sleep -Milliseconds 1200
        $teams=Capture $window '05-live-change-team.png'
        $hashes=@($teamSelection,$classSelection,$world,$scoreboard,$inventory,$walk,$pause,$teams)|ForEach-Object{(Get-FileHash $_ -Algorithm SHA256).Hash}
        if(@($hashes|Select-Object -Unique).Count -ne 8){throw 'A live join/gameplay/menu capture did not change.'}
    }
    [pscustomobject]@{Result='PASS';ProcessId=$process.Id;EvidenceDirectory=$evidencePath}
}
finally {
    if($null -ne $anchorProcess){
        if(-not $anchorProcess.WaitForExit(2000)){
            Stop-Process $anchorProcess.Id -Force -ErrorAction SilentlyContinue
        }
        $anchorProcess.Dispose()
    }
    if($window -ne [IntPtr]::Zero){[BattleSpades.LiveSmoke.Native]::Close($window)|Out-Null}
    if(-not $process.WaitForExit(5000)){Stop-Process $process.Id -Force -ErrorAction SilentlyContinue}
    $process.Dispose()
}
