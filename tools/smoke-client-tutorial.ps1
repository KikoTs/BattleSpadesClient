<#
.SYNOPSIS
Drives the native client into the playable 3D Tutorial world and back out.

.DESCRIPTION
Boots the real client, clicks the Tutorial square, waits through the bounded
Training.vxl load, presses the retail START gate, captures the rendered voxel world, walks forward with W to
prove the simulation advances (the diagnostics overlay changes), presses
Escape and confirms the Select Menu returns. The desktop must be interactive
and unlocked; the capture heuristics reject a black/unrendered world view.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string] $EvidenceDirectory,

    [ValidateRange(1, 60)]
    [int] $WindowTimeoutSeconds = 20,

    [ValidateRange(5, 180)]
    [int] $WorldLoadTimeoutSeconds = 60,

    [switch] $ExerciseWeaponSandbox,

    [switch] $CaptureWeaponWheel,

    [switch] $ExerciseAimAndClassSwitcher,

    [switch] $ExerciseGrenades,

    [switch] $ExerciseRocket,

    # Optional renderer tier override for end-to-end visual parity captures.
    # Keep this on the client command line instead of mutating client.toml so
    # the smoke cannot leak its graphics choice into a user's next session.
    [ValidateSet('', 'compatibility', 'low', 'medium', 'high', 'ultra')]
    [string] $ShaderQuality = '',

    # Optional map basename for atmosphere/lighting smoke captures.
    [string] $TutorialMap = '',

    # Optional repeatable map-lighting camera. Values use the native CLI
    # forms: "X,Y" for a standable column and "X,Y,Z" for the look target.
    [string] $TutorialStand = '',

    [string] $TutorialLook = '',

    # Close the offline tutorial help card before the primary world capture.
    # This is useful for clean atmosphere/lighting comparison screenshots.
    [switch] $HideTutorialPrompt,

    # Deterministically equip any retail tool without relying on F4/wheel
    # focus. Tool 5 is useful for block-line/palette visual verification.
    [ValidateRange(-1, 64)]
    [int] $TutorialToolId = -1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'smoke-client-tutorial.ps1 requires Windows.'
}

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$evidencePath = [System.IO.Path]::GetFullPath($EvidenceDirectory)
[System.IO.Directory]::CreateDirectory($evidencePath) | Out-Null
Add-Type -AssemblyName System.Drawing

if ($null -eq ('BattleSpades.TutorialSmoke.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace BattleSpades.TutorialSmoke
{
    [StructLayout(LayoutKind.Sequential)]
    public struct NativeRect
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
        public int Width { get { return Right - Left; } }
        public int Height { get { return Bottom - Top; } }
    }

    [StructLayout(LayoutKind.Sequential)]
    internal struct NativePoint { public int X; public int Y; }

    public static class NativeMethods
    {
        private const uint GwOwner = 4;
        private const uint SwpNoSize = 0x0001;
        private const uint SwpNoMove = 0x0002;
        private const uint SwpShowWindow = 0x0040;
        private const uint WmMouseMove = 0x0200;
        private const uint WmLeftButtonDown = 0x0201;
        private const uint WmLeftButtonUp = 0x0202;
        private const uint WmRightButtonDown = 0x0204;
        private const uint WmRightButtonUp = 0x0205;
        private const uint WmMouseWheel = 0x020A;
        private const uint WmClose = 0x0010;
        private const uint WmKeyDown = 0x0100;
        private const uint WmKeyUp = 0x0101;
        private const uint MkLeftButton = 0x0001;
        private const uint MkRightButton = 0x0002;
        private const uint KeyeventfKeyup = 0x0002;
        private const uint MouseeventfWheel = 0x0800;
        private const uint MouseeventfLeftDown = 0x0002;
        private const uint MouseeventfLeftUp = 0x0004;
        private const uint MouseeventfRightDown = 0x0008;
        private const uint MouseeventfRightUp = 0x0010;
        private static readonly IntPtr HwndTopMost = new IntPtr(-1);
        private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("kernel32.dll")] private static extern uint GetCurrentThreadId();
        [DllImport("user32.dll")] private static extern bool AttachThreadInput(
            uint attachThread, uint attachToThread, bool attach);
        [DllImport("user32.dll")] private static extern IntPtr SetFocus(IntPtr window);
        [DllImport("user32.dll")] private static extern IntPtr SetActiveWindow(IntPtr window);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetClientRect(IntPtr window, out NativeRect rectangle);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool ClientToScreen(IntPtr window, ref NativePoint point);
        [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetWindowPos(
            IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll")] private static extern uint MapVirtualKey(uint code, uint mapType);
        [DllImport("user32.dll")] private static extern void keybd_event(
            byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
        [DllImport("user32.dll")] private static extern void mouse_event(
            uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")] private static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")] private static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        public static void EnablePhysicalPixelCoordinates()
        {
            try { if (SetProcessDpiAwarenessContext(new IntPtr(-4))) return; }
            catch (EntryPointNotFoundException) { }
            SetProcessDPIAware();
        }

        public static IntPtr FindWindow(uint requiredProcessId)
        {
            IntPtr best = IntPtr.Zero;
            long bestArea = -1;
            EnumWindows(delegate(IntPtr candidate, IntPtr ignored)
            {
                uint processId;
                GetWindowThreadProcessId(candidate, out processId);
                if (processId != requiredProcessId || !IsWindowVisible(candidate) ||
                    GetWindow(candidate, GwOwner) != IntPtr.Zero) return true;
                NativeRect rectangle;
                if (!GetClientRect(candidate, out rectangle)) return true;
                long area = (long)Math.Max(0, rectangle.Width) * Math.Max(0, rectangle.Height);
                if (area > bestArea) { best = candidate; bestArea = area; }
                return true;
            }, IntPtr.Zero);
            return best;
        }

        public static bool ClientScreenRect(IntPtr window, out NativeRect screen)
        {
            screen = new NativeRect();
            NativeRect client;
            if (!GetClientRect(window, out client)) return false;
            NativePoint origin = new NativePoint { X = 0, Y = 0 };
            if (!ClientToScreen(window, ref origin)) return false;
            screen.Left = origin.X;
            screen.Top = origin.Y;
            screen.Right = origin.X + client.Width;
            screen.Bottom = origin.Y + client.Height;
            return client.Width > 0 && client.Height > 0;
        }

        private static IntPtr Coordinates(int x, int y)
        {
            uint packed = ((uint)(ushort)y << 16) | (ushort)x;
            return new IntPtr(unchecked((int)packed));
        }

        public static bool Click(IntPtr window, int x, int y)
        {
            NativePoint point = new NativePoint { X = x, Y = y };
            if (!ClientToScreen(window, ref point) || !SetCursorPos(point.X, point.Y)) return false;
            IntPtr packed = Coordinates(x, y);
            PostMessage(window, WmMouseMove, IntPtr.Zero, packed);
            PostMessage(window, WmLeftButtonDown, new IntPtr(MkLeftButton), packed);
            return PostMessage(window, WmLeftButtonUp, IntPtr.Zero, packed);
        }

        public static bool SendLeftButton(IntPtr window, int x, int y, bool down)
        {
            // Gameplay has SDL relative-mouse capture. Moving the physical
            // desktop cursor here can activate an unrelated foreground app;
            // only enqueue the client-relative messages for the game window.
            IntPtr packed = Coordinates(x, y);
            PostMessage(window, WmMouseMove, IntPtr.Zero, packed);
            return PostMessage(window, down ? WmLeftButtonDown : WmLeftButtonUp,
                               down ? new IntPtr(MkLeftButton) : IntPtr.Zero, packed);
        }

        public static bool SendRightButton(IntPtr window, int x, int y, bool down)
        {
            IntPtr packed = Coordinates(x, y);
            PostMessage(window, WmMouseMove, IntPtr.Zero, packed);
            return PostMessage(window, down ? WmRightButtonDown : WmRightButtonUp,
                               down ? new IntPtr(MkRightButton) : IntPtr.Zero, packed);
        }

        public static void BringForward(IntPtr window)
        {
            SetWindowPos(window, HwndTopMost, 0, 0, 0, 0,
                         SwpNoMove | SwpNoSize | SwpShowWindow);
            uint processId;
            uint targetThread = GetWindowThreadProcessId(window, out processId);
            uint callerThread = GetCurrentThreadId();
            bool attached = targetThread != 0U && callerThread != targetThread &&
                            AttachThreadInput(callerThread, targetThread, true);
            try
            {
                SetForegroundWindow(window);
                SetActiveWindow(window);
                SetFocus(window);
            }
            finally
            {
                if (attached) AttachThreadInput(callerThread, targetThread, false);
            }
        }

        public static bool SendWheel(IntPtr window, int x, int y, short delta)
        {
            NativePoint screen = new NativePoint { X = x, Y = y };
            if (!ClientToScreen(window, ref screen)) return false;
            uint wheel = ((uint)(ushort)delta) << 16;
            return PostMessage(window, WmMouseWheel,
                               new IntPtr(unchecked((int)wheel)),
                               Coordinates(screen.X, screen.Y));
        }

        public static bool SendKey(IntPtr window, uint virtualKey, bool down)
        {
            uint scan = MapVirtualKey(virtualKey, 0);
            if (down)
            {
                int flags = unchecked((int)(1U | (scan << 16)));
                return PostMessage(window, WmKeyDown, new IntPtr(virtualKey), new IntPtr(flags));
            }
            int upFlags = unchecked((int)(1U | (scan << 16) | (1U << 30) | (1U << 31)));
            return PostMessage(window, WmKeyUp, new IntPtr(virtualKey), new IntPtr(upFlags));
        }

        public static void SendPhysicalKey(uint virtualKey, bool down)
        {
            keybd_event((byte)virtualKey, (byte)MapVirtualKey(virtualKey, 0),
                        down ? 0U : KeyeventfKeyup, UIntPtr.Zero);
        }

        public static void SendPhysicalWheel(short delta)
        {
            mouse_event(MouseeventfWheel, 0U, 0U, unchecked((uint)(int)delta), UIntPtr.Zero);
        }

        public static void SendPhysicalRightButton(bool down)
        {
            mouse_event(down ? MouseeventfRightDown : MouseeventfRightUp,
                        0U, 0U, 0U, UIntPtr.Zero);
        }

        public static void SendPhysicalLeftButton(bool down)
        {
            mouse_event(down ? MouseeventfLeftDown : MouseeventfLeftUp,
                        0U, 0U, 0U, UIntPtr.Zero);
        }

        public static bool Close(IntPtr window) { return PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero); }
    }
}
'@
}

function Get-ClientRectangle {
    param([IntPtr] $WindowHandle)
    $rectangle = [BattleSpades.TutorialSmoke.NativeRect]::new()
    if (-not [BattleSpades.TutorialSmoke.NativeMethods]::ClientScreenRect(
            $WindowHandle, [ref] $rectangle)) {
        throw 'Could not query the native client rectangle.'
    }
    return $rectangle
}

function ConvertFrom-DesignPoint {
    param([double] $X, [double] $Y, [int] $Width, [int] $Height)
    $scale = [Math]::Min($Width / 800.0, $Height / 600.0)
    $left = ($Width - (800.0 * $scale)) / 2.0
    $top = ($Height - (600.0 * $scale)) / 2.0
    return [System.Drawing.Point]::new(
        [int] [Math]::Round($left + ($X * $scale)),
        [int] [Math]::Round($top + ($Y * $scale)))
}

function Save-ClientCapture {
    param([IntPtr] $WindowHandle, [string] $Name)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $bitmap = [System.Drawing.Bitmap]::new($rectangle.Width, $rectangle.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $rectangle.Left, $rectangle.Top, 0, 0,
            [System.Drawing.Size]::new($rectangle.Width, $rectangle.Height),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
        $path = [System.IO.Path]::Combine($evidencePath, $Name)
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $path
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Get-CaptureBrightness {
    param([string] $Path)
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    try {
        $total = 0.0
        $samples = 0
        for ($y = 0; $y -lt $bitmap.Height; $y += [Math]::Max(1, [int]($bitmap.Height / 24))) {
            for ($x = 0; $x -lt $bitmap.Width; $x += [Math]::Max(1, [int]($bitmap.Width / 24))) {
                $pixel = $bitmap.GetPixel($x, $y)
                $total += ($pixel.R + $pixel.G + $pixel.B) / 3.0
                $samples += 1
            }
        }
        return $total / [Math]::Max(1, $samples)
    }
    finally {
        $bitmap.Dispose()
    }
}

function Click-Design {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $point = ConvertFrom-DesignPoint -X $X -Y $Y -Width $rectangle.Width -Height $rectangle.Height
    if (-not [BattleSpades.TutorialSmoke.NativeMethods]::Click(
            $WindowHandle, $point.X, $point.Y)) {
        throw "Failed to click design point ($X, $Y)."
    }
}

function Set-DesignLeftButton {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y, [bool] $Down)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $point = ConvertFrom-DesignPoint -X $X -Y $Y -Width $rectangle.Width -Height $rectangle.Height
    if (-not [BattleSpades.TutorialSmoke.NativeMethods]::SendLeftButton(
            $WindowHandle, $point.X, $point.Y, $Down)) {
        throw "Failed to send left mouse state at design point ($X, $Y)."
    }
}

function Set-DesignRightButton {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y, [bool] $Down)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $point = ConvertFrom-DesignPoint -X $X -Y $Y -Width $rectangle.Width -Height $rectangle.Height
    if (-not [BattleSpades.TutorialSmoke.NativeMethods]::SendRightButton(
            $WindowHandle, $point.X, $point.Y, $Down)) {
        throw "Failed to send right mouse state at design point ($X, $Y)."
    }
}

function Send-DesignWheel {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y, [int] $Delta)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $point = ConvertFrom-DesignPoint -X $X -Y $Y -Width $rectangle.Width -Height $rectangle.Height
    if (-not [BattleSpades.TutorialSmoke.NativeMethods]::SendWheel(
            $WindowHandle, $point.X, $point.Y, [int16] $Delta)) {
        throw "Failed to send wheel input at design point ($X, $Y)."
    }
}

function Assert-Alive {
    param([System.Diagnostics.Process] $Process, [string] $Stage)
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "Client exited with $($Process.ExitCode) during $Stage."
    }
}

$vkW = 0x57
$vkEscape = 0x1B
$vkF4 = 0x73
$vkF5 = 0x74
$vk9 = 0x39

[BattleSpades.TutorialSmoke.NativeMethods]::EnablePhysicalPixelCoordinates()
$arguments = @('--run-forever', '--pace')
if (-not [string]::IsNullOrWhiteSpace($ShaderQuality)) {
    $arguments += @('--shader-quality', $ShaderQuality)
}
if (-not [string]::IsNullOrWhiteSpace($TutorialMap)) {
    $arguments += @('--tutorial-map', $TutorialMap)
}
if (-not [string]::IsNullOrWhiteSpace($TutorialStand)) {
    $arguments += @('--tutorial-stand', $TutorialStand)
}
if (-not [string]::IsNullOrWhiteSpace($TutorialLook)) {
    $arguments += @('--tutorial-look', $TutorialLook)
}
if ($TutorialToolId -ge 0) {
    $arguments += @('--tutorial-tool', [string]$TutorialToolId)
} elseif ($ExerciseGrenades -and $ExerciseAimAndClassSwitcher) {
    throw 'ExerciseGrenades and ExerciseAimAndClassSwitcher require separate deterministic tool runs.'
}
if ($TutorialToolId -ge 0) {
    # The explicit tool selection above owns the deterministic setup.
} elseif ($ExerciseGrenades) {
    # Deterministic automation boundary: do not depend on Windows granting
    # synthetic function keys to SDL's relative-input window.
    $arguments += @('--tutorial-tool', '11')
} elseif ($ExerciseRocket) {
    $arguments += @('--tutorial-tool', '12')
} elseif ($ExerciseAimAndClassSwitcher) {
    $arguments += @('--tutorial-tool', '18', '--tutorial-aim')
} elseif ($ExerciseWeaponSandbox) {
    # This enters through debug_grant_full_loadout(), the same operation as
    # F4, while avoiding Windows/SDL swallowing synthetic function keys.
    $arguments += @('--tutorial-tool', '8')
}
$process = Start-Process -FilePath $executablePath -ArgumentList $arguments `
    -WorkingDirectory ([System.IO.Path]::GetDirectoryName($executablePath)) -PassThru
$windowHandle = [IntPtr]::Zero

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($WindowTimeoutSeconds)
    do {
        Assert-Alive -Process $process -Stage 'startup'
        $windowHandle = [BattleSpades.TutorialSmoke.NativeMethods]::FindWindow([uint32] $process.Id)
        if ($windowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($windowHandle -eq [IntPtr]::Zero) { throw 'Timed out waiting for the client window.' }

    [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
    # Cold native starts preload the complete weapon/audio catalog before the
    # select menu becomes interactive. The former 1.8 s delay occasionally
    # clicked the splash screen and then mislabeled menu captures as gameplay.
    Start-Sleep -Milliseconds 5000
    $menuCapture = Save-ClientCapture $windowHandle '00-select-menu.png'

    # The Tutorial square: retail model y is bottom-origin (600 - 123 = 477).
    Click-Design $windowHandle 299 477
    Start-Sleep -Milliseconds 700
    Assert-Alive -Process $process -Stage 'tutorial loading entry'
    [void] (Save-ClientCapture $windowHandle '01-tutorial-loading.png')

    # The bounded loader parses and meshes Training.vxl off-thread. Loading
    # completion deliberately does not enter the world: retail leaves a
    # glowing START gate at x=492..738, y=449..507 until the player confirms.
    Start-Sleep -Seconds ([Math]::Min($WorldLoadTimeoutSeconds, 25))
    Assert-Alive -Process $process -Stage 'tutorial loading completion'
    Click-Design $windowHandle 615 478
    if ($ExerciseWeaponSandbox) {
        # Capture while RetailInventory.pullout_remaining is still live.
        Start-Sleep -Milliseconds 100
    } else {
        Start-Sleep -Milliseconds 1800
    }
    Assert-Alive -Process $process -Stage 'tutorial START gate'
    [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
    if ($ExerciseWeaponSandbox) {
        Start-Sleep -Milliseconds 30
    } else {
        Start-Sleep -Milliseconds 700
    }
    if ($HideTutorialPrompt) {
        $vkH = 0x48
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalKey($vkH, $true)
        Start-Sleep -Milliseconds 60
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalKey($vkH, $false)
        Start-Sleep -Milliseconds 250
        Assert-Alive -Process $process -Stage 'tutorial help dismissal'
    }
    $worldCapture = Save-ClientCapture $windowHandle '02-tutorial-world.png'

    if ($CaptureWeaponWheel) {
        [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
        Start-Sleep -Milliseconds 250
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalWheel(-120)
        Start-Sleep -Milliseconds 250
        Assert-Alive -Process $process -Stage 'weapon wheel capture'
        $wheelCapture = Save-ClientCapture $windowHandle '02a-weapon-wheel.png'
        if ((Get-FileHash -LiteralPath $worldCapture -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $wheelCapture -Algorithm SHA256).Hash) {
            throw 'Mouse wheel did not visibly open the weapon toolbar.'
        }
    }

    if ($ExerciseWeaponSandbox) {
        [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
        # The first capture pins the same all-weapons acquisition/pullout
        # state F4 creates. The settled capture then verifies the final grip.
        $acquiredCapture = Save-ClientCapture $windowHandle '02a-all-weapons-acquired.png'
        Start-Sleep -Milliseconds 450
        Assert-Alive -Process $process -Stage 'all-weapons sandbox activation'
        $sandboxCapture = Save-ClientCapture $windowHandle '02a-weapon-sandbox.png'

        # Capture a real wheel transition before it settles. This is the exact
        # regression where the weapon used to draw immediately and the hands
        # followed later because only the arm hierarchy consumed pullout.
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalWheel(-120)
        Start-Sleep -Milliseconds 35
        Assert-Alive -Process $process -Stage 'shared weapon/hand pullout transition'
        [void] (Save-ClientCapture $windowHandle '02a1-weapon-hand-transition.png')
        Start-Sleep -Milliseconds 400
        [void] (Save-ClientCapture $windowHandle '02a2-weapon-hand-settled.png')
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalWheel(120)
        Start-Sleep -Milliseconds 400

        # Retail RMB spins the barrels without consuming ammunition. Keep it
        # held while pressing LMB so the ready motor can fire immediately.
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalRightButton($true)
        Start-Sleep -Milliseconds 1200
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($true)
        Start-Sleep -Milliseconds 450
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($false)
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalRightButton($false)
        Start-Sleep -Milliseconds 250
        Assert-Alive -Process $process -Stage 'sandbox weapon fire'
        $sandboxFireCapture = Save-ClientCapture $windowHandle '02b-weapon-sandbox-fire.png'
        if ((Get-FileHash -LiteralPath $sandboxCapture -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $sandboxFireCapture -Algorithm SHA256).Hash) {
            throw 'Weapon sandbox fire produced no visible simulation update.'
        }

        # Sweep through more than the full 65-tool catalogue. This regresses
        # the original crash where opening the wheel rendered all entries and
        # passed iconless tool rows to the texture loader.
        for ($notch = 0; $notch -lt 70; $notch += 1) {
            [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalWheel(-120)
            Start-Sleep -Milliseconds 25
            Assert-Alive -Process $process -Stage "weapon wheel sweep $notch"
        }
        Start-Sleep -Milliseconds 500
        Assert-Alive -Process $process -Stage 'complete 65-tool wheel sweep'
        [void] (Save-ClientCapture $windowHandle '02c-weapon-scroll-sweep.png')
    }

    if ($ExerciseAimAndClassSwitcher) {
        [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
        # --tutorial-tool 18 eliminates focus-sensitive F4/wheel setup and
        # starts on the recovered sniper scope deterministically.
        Start-Sleep -Milliseconds 350
        Start-Sleep -Milliseconds 500
        Assert-Alive -Process $process -Stage 'sniper scope activation'
        $scopeCapture = Save-ClientCapture $windowHandle '02d-sniper-scope.png'
        if ((Get-CaptureBrightness -Path $scopeCapture) -lt 20.0) {
            throw 'Sniper scope capture is black; the rendered aim path was not visible.'
        }
        if ((Get-FileHash -LiteralPath $worldCapture -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $scopeCapture -Algorithm SHA256).Hash) {
            throw 'Deterministic sniper aim produced no visible scope/FOV change.'
        }

        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalKey($vkF5, $true)
        Start-Sleep -Milliseconds 100
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalKey($vkF5, $false)
        Start-Sleep -Milliseconds 650
        Assert-Alive -Process $process -Stage 'FPS class switch'
        $classCapture = Save-ClientCapture $windowHandle '02e-scout-arms.png'
        if ((Get-CaptureBrightness -Path $classCapture) -lt 20.0) {
            throw 'F5 class-switch capture is black; the rendered class path was not visible.'
        }
    }

    if ($ExerciseGrenades) {
        [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
        Start-Sleep -Milliseconds 500
        # --tutorial-tool 11 grants and equips the complete debug inventory as
        # the local session is created, so this capture genuinely exercises a
        # grenade even when Windows refuses synthetic function-key focus.
        $grenadeReady = Save-ClientCapture $windowHandle '02f-grenade-ready.png'
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($true)
        Start-Sleep -Milliseconds 500
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($false)
        Start-Sleep -Milliseconds 130
        Assert-Alive -Process $process -Stage 'cooked grenade throw'
        $grenadeFlight = Save-ClientCapture $windowHandle '02g-grenade-flight.png'
        if ((Get-FileHash -LiteralPath $grenadeReady -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $grenadeFlight -Algorithm SHA256).Hash) {
            throw 'Cooked grenade release produced no visible model/HUD update.'
        }
        # A 0.5 s cook leaves a two-second fuse. Capture just as the bounded
        # explosion particles are alive; waiting 2.2 s observed the correct
        # detonation only after its short retail-style effect had expired.
        Start-Sleep -Milliseconds 1800
        Assert-Alive -Process $process -Stage 'cooked grenade explosion'
        $grenadeExplosion = Save-ClientCapture $windowHandle '02h-grenade-explosion.png'
        # The projectile can detonate outside the current frustum after a
        # bounce. Keeping the client alive past the fuse is the graphical
        # invariant; the deterministic session regression asserts that the
        # explosion particle/sound event was actually dispatched.
    }

    if ($ExerciseRocket) {
        [BattleSpades.TutorialSmoke.NativeMethods]::BringForward($windowHandle)
        Start-Sleep -Milliseconds 350
        $rocketReady = Save-ClientCapture $windowHandle '02i-rocket-ready.png'
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($true)
        Start-Sleep -Milliseconds 40
        [BattleSpades.TutorialSmoke.NativeMethods]::SendPhysicalLeftButton($false)
        Assert-Alive -Process $process -Stage 'rocket launch'
        [void] (Save-ClientCapture $windowHandle '02j-rocket-flight-040ms.png')
        Start-Sleep -Milliseconds 70
        [void] (Save-ClientCapture $windowHandle '02k-rocket-impact-110ms.png')
        Start-Sleep -Milliseconds 100
        $rocketBlast = Save-ClientCapture $windowHandle '02l-rocket-blast-210ms.png'
        Start-Sleep -Milliseconds 240
        [void] (Save-ClientCapture $windowHandle '02m-rocket-smoke-450ms.png')
        Assert-Alive -Process $process -Stage 'rocket blast and smoke'
        if ((Get-FileHash -LiteralPath $rocketReady -Algorithm SHA256).Hash -eq
            (Get-FileHash -LiteralPath $rocketBlast -Algorithm SHA256).Hash) {
            throw 'RPG fire produced no visible projectile or blast update.'
        }
    }

    # Walk forward: the fixed 60 Hz simulation must move the player, which
    # the diagnostics overlay makes visible in pixels.
    [void] [BattleSpades.TutorialSmoke.NativeMethods]::SendKey($windowHandle, $vkW, $true)
    Start-Sleep -Milliseconds 900
    [void] [BattleSpades.TutorialSmoke.NativeMethods]::SendKey($windowHandle, $vkW, $false)
    Start-Sleep -Milliseconds 400
    Assert-Alive -Process $process -Stage 'tutorial walk'
    $walkCapture = Save-ClientCapture $windowHandle '03-tutorial-after-walk.png'

    [void] [BattleSpades.TutorialSmoke.NativeMethods]::SendKey($windowHandle, $vkEscape, $true)
    [void] [BattleSpades.TutorialSmoke.NativeMethods]::SendKey($windowHandle, $vkEscape, $false)
    Start-Sleep -Milliseconds 500
    Assert-Alive -Process $process -Stage 'pause menu entry'
    $pauseCapture = Save-ClientCapture $windowHandle '04-pause-menu.png'
    # Offline Tutorial hides the two server-owned selectors, leaving the
    # recovered Resume/Settings/Disconnect actions at their retail positions.
    Click-Design $windowHandle 400 447
    Start-Sleep -Milliseconds 1200
    Assert-Alive -Process $process -Stage 'pause disconnect to select menu'
    $returnCapture = Save-ClientCapture $windowHandle '05-select-menu-return.png'

    $worldHash = (Get-FileHash -LiteralPath $worldCapture -Algorithm SHA256).Hash
    $menuHash = (Get-FileHash -LiteralPath $menuCapture -Algorithm SHA256).Hash
    $walkHash = (Get-FileHash -LiteralPath $walkCapture -Algorithm SHA256).Hash
    $pauseHash = (Get-FileHash -LiteralPath $pauseCapture -Algorithm SHA256).Hash
    if ($worldHash -eq $menuHash) {
        throw 'The tutorial world capture is identical to the menu; the world never rendered.'
    }
    if ($worldHash -eq $walkHash) {
        throw 'Walking changed nothing on screen; the simulation did not advance.'
    }
    if ($pauseHash -eq $worldHash) {
        throw 'Escape did not render the in-world pause overlay.'
    }
    $worldBrightness = Get-CaptureBrightness -Path $worldCapture
    if ($worldBrightness -lt 20.0) {
        throw "The tutorial world capture is nearly black (brightness $worldBrightness); the world pass did not draw."
    }

    [pscustomobject]@{
        Result = 'PASS'
        ProcessId = $process.Id
        WorldBrightness = [Math]::Round($worldBrightness, 1)
        EvidenceDirectory = $evidencePath
    }
}
finally {
    if ($windowHandle -ne [IntPtr]::Zero) {
        [void] [BattleSpades.TutorialSmoke.NativeMethods]::Close($windowHandle)
    }
    if (-not $process.WaitForExit(5000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        [void] $process.WaitForExit(5000)
    }
    $process.Dispose()
}
