<#
.SYNOPSIS
Drives the real native frontend through its recovered nested menu routes.

.DESCRIPTION
Clicks the SDL/Win32 client in design-space coordinates and captures the main,
Join Match, Server Browser, Direct Connect, Quick Play, Create Match, nested mode
picker, UGC Select, Map Creator browser/host lobby, Publish Map, Leaderboard,
Player Profile, and match-loading screens.
Mid-transition captures prove the retained old and new layers are rendered
together. The desktop must be interactive and unlocked.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string] $EvidenceDirectory,

    [ValidateRange(1, 60)]
    [int] $WindowTimeoutSeconds = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'smoke-client-navigation.ps1 requires Windows.'
}

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$evidencePath = [System.IO.Path]::GetFullPath($EvidenceDirectory)
[System.IO.Directory]::CreateDirectory($evidencePath) | Out-Null
Add-Type -AssemblyName System.Drawing

if ($null -eq ('BattleSpades.NavigationSmoke.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace BattleSpades.NavigationSmoke
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
        private const uint WmMouseMove = 0x0200;
        private const uint WmLeftButtonDown = 0x0201;
        private const uint WmLeftButtonUp = 0x0202;
        private const uint WmClose = 0x0010;
        private const uint MkLeftButton = 0x0001;
        private const uint SwpNoMove = 0x0002;
        private const uint SwpNoZOrder = 0x0004;
        private const uint SwpNoActivate = 0x0010;
        private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")] private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
        [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")] private static extern IntPtr GetWindow(IntPtr window, uint command);
        [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetClientRect(IntPtr window, out NativeRect rectangle);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(IntPtr window, out NativeRect rectangle);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool PrintWindow(IntPtr window, IntPtr deviceContext, uint flags);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool ClientToScreen(IntPtr window, ref NativePoint point);
        [DllImport("user32.dll")] private static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll")] private static extern uint MapVirtualKey(uint code, uint mapType);
        [DllImport("user32.dll")] private static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll", SetLastError = true)] private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
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

        public static bool WindowScreenRect(IntPtr window, out NativeRect screen)
        {
            return GetWindowRect(window, out screen) && screen.Width > 0 && screen.Height > 0;
        }

        public static bool CaptureWindow(IntPtr window, IntPtr deviceContext)
        {
            // PW_RENDERFULLCONTENT asks DWM/composited windows for the complete
            // target surface even when another app overlaps it.
            return PrintWindow(window, deviceContext, 2U);
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

        public static void BringForward(IntPtr window) { SetForegroundWindow(window); }
        public static bool ResizeClient(IntPtr window, int width, int height)
        {
            NativeRect client;
            NativeRect outer;
            if (!GetClientRect(window, out client) || !GetWindowRect(window, out outer)) return false;
            int targetWidth = outer.Width + width - client.Width;
            int targetHeight = outer.Height + height - client.Height;
            return SetWindowPos(window, IntPtr.Zero, 0, 0, targetWidth, targetHeight,
                                SwpNoMove | SwpNoZOrder | SwpNoActivate);
        }
        private static bool PressVirtualKey(IntPtr window, uint virtualKey)
        {
            uint scan = MapVirtualKey(virtualKey, 0);
            // Windows may return false when the target is already foreground
            // or when focus-stealing policy declines the request. The
            // physical key event is still valid for the active smoke window.
            SetForegroundWindow(window);

            // SDL3 consumes the physical keyboard stream. Posting WM_KEYDOWN
            // directly to the HWND can return success while SDL receives no
            // event, leaving every later smoke capture on the same screen.
            // keybd_event is intentionally used only by this interactive
            // Windows smoke helper after the target window is foregrounded.
            keybd_event((byte)virtualKey, (byte)scan, 0U, UIntPtr.Zero);
            System.Threading.Thread.Sleep(25);
            keybd_event((byte)virtualKey, (byte)scan, 2U, UIntPtr.Zero);
            return true;
        }
        public static bool PressF12(IntPtr window) { return PressVirtualKey(window, 0x7B); }
        public static bool PressEscape(IntPtr window) { return PressVirtualKey(window, 0x1B); }
        public static bool Close(IntPtr window) { return PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero); }
    }
}
'@
}

function Get-ClientRectangle {
    param([IntPtr] $WindowHandle)
    $rectangle = [BattleSpades.NavigationSmoke.NativeRect]::new()
    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::ClientScreenRect(
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
    $client = Get-ClientRectangle -WindowHandle $WindowHandle
    $outer = [BattleSpades.NavigationSmoke.NativeRect]::new()
    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::WindowScreenRect(
            $WindowHandle, [ref] $outer)) {
        throw 'Could not query the target window rectangle.'
    }
    $outerBitmap = [System.Drawing.Bitmap]::new($outer.Width, $outer.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($outerBitmap)
    $deviceContext = $graphics.GetHdc()
    try {
        if (-not [BattleSpades.NavigationSmoke.NativeMethods]::CaptureWindow(
                $WindowHandle, $deviceContext)) {
            throw 'PrintWindow could not capture the target client.'
        }
    }
    finally {
        $graphics.ReleaseHdc($deviceContext)
        $graphics.Dispose()
    }
    $clientBitmap = [System.Drawing.Bitmap]::new($client.Width, $client.Height)
    $clientGraphics = [System.Drawing.Graphics]::FromImage($clientBitmap)
    try {
        $source = [System.Drawing.Rectangle]::new(
            $client.Left - $outer.Left,
            $client.Top - $outer.Top,
            $client.Width,
            $client.Height)
        $destination = [System.Drawing.Rectangle]::new(0, 0, $client.Width, $client.Height)
        $clientGraphics.DrawImage($outerBitmap, $destination, $source, [System.Drawing.GraphicsUnit]::Pixel)
        $path = [System.IO.Path]::Combine($evidencePath, $Name)
        $clientBitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $path
    }
    finally {
        $clientGraphics.Dispose()
        $clientBitmap.Dispose()
        $outerBitmap.Dispose()
    }
}

function Click-Design {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $point = ConvertFrom-DesignPoint -X $X -Y $Y -Width $rectangle.Width -Height $rectangle.Height
    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::Click(
            $WindowHandle, $point.X, $point.Y)) {
        throw "Failed to click design point ($X, $Y)."
    }
}

function Press-Back {
    param([IntPtr] $WindowHandle)
    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::PressEscape($WindowHandle)) {
        throw 'Failed to send the Escape navigation action.'
    }
}

function Assert-Alive {
    param([System.Diagnostics.Process] $Process, [string] $Stage)
    $Process.Refresh()
    if ($Process.HasExited) {
        throw "Client exited with $($Process.ExitCode) during $Stage."
    }
}

[BattleSpades.NavigationSmoke.NativeMethods]::EnablePhysicalPixelCoordinates()
$process = Start-Process -FilePath $executablePath -ArgumentList '--run-forever', '--pace' `
    -WorkingDirectory ([System.IO.Path]::GetDirectoryName($executablePath)) -PassThru
$windowHandle = [IntPtr]::Zero
$captures = [System.Collections.Generic.List[string]]::new()

try {
    $deadline = [DateTime]::UtcNow.AddSeconds($WindowTimeoutSeconds)
    do {
        Assert-Alive -Process $process -Stage 'startup'
        $windowHandle = [BattleSpades.NavigationSmoke.NativeMethods]::FindWindow([uint32] $process.Id)
        if ($windowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($windowHandle -eq [IntPtr]::Zero) { throw 'Timed out waiting for the client window.' }

    [BattleSpades.NavigationSmoke.NativeMethods]::BringForward($windowHandle)
    Start-Sleep -Milliseconds 300
    $captures.Add((Save-ClientCapture $windowHandle '00-boot-loading.png'))
    Start-Sleep -Milliseconds 1500
    $captures.Add((Save-ClientCapture $windowHandle '01-main.png'))
    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::ResizeClient(
            $windowHandle, 1280, 720)) {
        throw 'Failed to resize the client to the widescreen transition fixture.'
    }
    Start-Sleep -Milliseconds 500
    $captures.Add((Save-ClientCapture $windowHandle '01a-main-widescreen.png'))

    Click-Design $windowHandle 400 208
    Start-Sleep -Milliseconds 70
    $captures.Add((Save-ClientCapture $windowHandle '02-main-to-join-transition.png'))
    # Retail exposes the incoming menu once abs(current_x) < 0.5, long before
    # the retained previous layer is released at 0.005. This click deliberately
    # lands during that interactive half of the animation.
    Start-Sleep -Milliseconds 90
    $captures.Add((Save-ClientCapture $windowHandle '03-join-interactive-halfway.png'))
    Click-Design $windowHandle 400 195
    Start-Sleep -Milliseconds 70
    $captures.Add((Save-ClientCapture $windowHandle '04-join-to-browser-transition.png'))
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '05-server-browser.png'))
    Click-Design $windowHandle 110 552
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '05b-join-match-settled.png'))

    Click-Design $windowHandle 400 258
    Start-Sleep -Milliseconds 70
    $captures.Add((Save-ClientCapture $windowHandle '06-join-to-direct-transition.png'))
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '07-direct-connect.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950

    Click-Design $windowHandle 400 321
    Start-Sleep -Milliseconds 70
    $captures.Add((Save-ClientCapture $windowHandle '08-join-to-quick-play-transition.png'))
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '09-quick-play.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950

    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950

    Click-Design $windowHandle 400 271
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '10-create-match.png'))
    # The restored lobby keeps mode choices in the right-hand settings pane.
    # Click the retail pencil button, not the value label beside it.
    Click-Design $windowHandle 675 205
    Start-Sleep -Milliseconds 70
    $captures.Add((Save-ClientCapture $windowHandle '11-create-mode-transition.png'))
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '12-create-mode-picker.png'))
    # Confirm closes the nested pane; LEAVE LOBBY then returns to SelectMenu.
    Click-Design $windowHandle 570 480
    Start-Sleep -Milliseconds 950
    Click-Design $windowHandle 120 557
    Start-Sleep -Milliseconds 950

    Click-Design $windowHandle 400 397
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '13-ugc-select.png'))
    Click-Design $windowHandle 400 195
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '13a-ugc-editor-browser.png'))
    Click-Design $windowHandle 650 480
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '13b-ugc-editor-lobby.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '13c-ugc-editor-browser-back.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950
    Click-Design $windowHandle 400 258
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '14-ugc-publish.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950

    Click-Design $windowHandle 434 477
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '15-leaderboard.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 950

    Click-Design $windowHandle 400 334
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '16-player-profile.png'))
    Press-Back $windowHandle
    Start-Sleep -Milliseconds 1200

    Click-Design $windowHandle 400 271
    Start-Sleep -Milliseconds 950
    Click-Design $windowHandle 570 480
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '17-match-loading.png'))

    if (-not [BattleSpades.NavigationSmoke.NativeMethods]::PressF12($windowHandle)) {
        throw 'Failed to send the F12 parity-browser shortcut.'
    }
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '18-parity-debug-browser.png'))
    Click-Design $windowHandle 650 480
    Start-Sleep -Milliseconds 250
    $captures.Add((Save-ClientCapture $windowHandle '19-parity-debug-hitboxes.png'))
    Click-Design $windowHandle 485 480
    Start-Sleep -Milliseconds 950
    $captures.Add((Save-ClientCapture $windowHandle '20-parity-open-native.png'))

    Assert-Alive -Process $process -Stage 'the complete navigation sequence'
    $semanticCaptures = @(
        '01a-main-widescreen.png',
        '05-server-browser.png',
        '05b-join-match-settled.png',
        '07-direct-connect.png',
        '09-quick-play.png',
        '10-create-match.png',
        '12-create-mode-picker.png',
        '13-ugc-select.png',
        '13a-ugc-editor-browser.png',
        '13b-ugc-editor-lobby.png',
        '14-ugc-publish.png',
        '15-leaderboard.png',
        '16-player-profile.png',
        '17-match-loading.png',
        '18-parity-debug-browser.png'
    )
    $semanticHashes = @(
        foreach ($name in $semanticCaptures) {
            (Get-FileHash -LiteralPath ([System.IO.Path]::Combine($evidencePath, $name)) `
                -Algorithm SHA256).Hash
        }
    )
    if (($semanticHashes | Select-Object -Unique).Count -ne $semanticHashes.Count) {
        throw 'Two settled semantic captures are identical; navigation did not reach every expected screen.'
    }
    [pscustomobject]@{
        Result = 'PASS'
        ProcessId = $process.Id
        CaptureCount = $captures.Count
        EvidenceDirectory = $evidencePath
    }
}
finally {
    if ($windowHandle -ne [IntPtr]::Zero) {
        [void] [BattleSpades.NavigationSmoke.NativeMethods]::Close($windowHandle)
    }
    if (-not $process.WaitForExit(5000)) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
        [void] $process.WaitForExit(5000)
    }
    $process.Dispose()
}
