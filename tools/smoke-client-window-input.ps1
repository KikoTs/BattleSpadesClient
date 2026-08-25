<#
.SYNOPSIS
Exercises the real BattleSpadesClient Win32/SDL window and pointer lifecycle.

.DESCRIPTION
Launches the graphical client and validates behavior that event-injection unit
tests cannot cover: exact client-area resize, minimize/restore survival,
focus-loss cancellation of a held menu button, hover hit-testing after restore,
and a graceful process exit through the visible Quit control.

The script captures only the Join Match button region for state comparison.
Pass EvidenceDirectory to retain the hover, pressed, and restored PNGs.

.EXAMPLE
.\tools\smoke-client-window-input.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -EvidenceDirectory .\out\platform-check\window-input

.NOTES
Windows only. The desktop must be interactive and unlocked because this smoke
checks the pixels the player actually sees.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [string[]] $ArgumentList = @(),
    [string] $WorkingDirectory = '',
    [string] $EvidenceDirectory = '',

    [ValidateRange(640, 3840)]
    [int] $ClientWidth = 1000,

    [ValidateRange(480, 2160)]
    [int] $ClientHeight = 700,

    [ValidateRange(1, 60)]
    [int] $WindowTimeoutSeconds = 20,

    [ValidateRange(1, 20)]
    [int] $ShutdownTimeoutSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'smoke-client-window-input.ps1 requires Windows.'
}

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
if (-not [System.IO.File]::Exists($executablePath)) {
    throw "Executable is not a file: $executablePath"
}

if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    $workingDirectoryPath = [System.IO.Path]::GetDirectoryName($executablePath)
}
else {
    $workingDirectoryPath = (Resolve-Path -LiteralPath $WorkingDirectory -ErrorAction Stop).Path
}

$evidencePath = ''
if (-not [string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    if ([System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
        $evidencePath = [System.IO.Path]::GetFullPath($EvidenceDirectory)
    }
    else {
        $evidencePath = [System.IO.Path]::GetFullPath(
            [System.IO.Path]::Combine((Get-Location).Path, $EvidenceDirectory))
    }
    [System.IO.Directory]::CreateDirectory($evidencePath) | Out-Null
}

Add-Type -AssemblyName System.Drawing

if ($null -eq ('BattleSpades.WindowSmoke.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace BattleSpades.WindowSmoke
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
    internal struct NativePoint
    {
        public int X;
        public int Y;
    }

    public static class NativeMethods
    {
        private const uint GwOwner = 4;
        private const uint SwpNoSize = 0x0001;
        private const uint SwpNoMove = 0x0002;
        private const uint SwpNoZOrder = 0x0004;
        private const uint SwpShowWindow = 0x0040;
        private const int SwMinimize = 6;
        private const int SwRestore = 9;
        private const uint WmClose = 0x0010;
        private const uint WmMouseMove = 0x0200;
        private const uint WmLeftButtonDown = 0x0201;
        private const uint WmLeftButtonUp = 0x0202;
        private const uint MkLeftButton = 0x0001;

        private static readonly IntPtr HwndTopMost = new IntPtr(-1);
        private delegate bool EnumWindowsProc(IntPtr window, IntPtr parameter);

        [DllImport("user32.dll")]
        private static extern bool EnumWindows(EnumWindowsProc callback, IntPtr parameter);
        [DllImport("user32.dll")]
        private static extern bool IsWindowVisible(IntPtr window);
        [DllImport("user32.dll")]
        public static extern bool IsWindow(IntPtr window);
        [DllImport("user32.dll")]
        private static extern IntPtr GetWindow(IntPtr window, uint command);
        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetClientRect(IntPtr window, out NativeRect rectangle);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetWindowRect(IntPtr window, out NativeRect rectangle);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool ClientToScreen(IntPtr window, ref NativePoint point);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetWindowPos(
            IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
        [DllImport("user32.dll")]
        public static extern bool IsIconic(IntPtr window);
        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr window, int command);
        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr window);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool PostMessage(
            IntPtr window, uint message, IntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")]
        private static extern bool SetProcessDPIAware();
        [DllImport("user32.dll")]
        private static extern bool SetProcessDpiAwarenessContext(IntPtr value);

        public static void EnablePhysicalPixelCoordinates()
        {
            try
            {
                // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
                if (SetProcessDpiAwarenessContext(new IntPtr(-4)))
                    return;
            }
            catch (EntryPointNotFoundException)
            {
            }
            SetProcessDPIAware();
        }

        public static IntPtr FindBestWindow(uint requiredProcessId)
        {
            IntPtr best = IntPtr.Zero;
            long bestArea = -1;
            EnumWindows(delegate(IntPtr candidate, IntPtr ignored)
            {
                uint processId;
                GetWindowThreadProcessId(candidate, out processId);
                if (processId != requiredProcessId || !IsWindowVisible(candidate) ||
                    GetWindow(candidate, GwOwner) != IntPtr.Zero)
                    return true;

                NativeRect client;
                if (!GetClientRect(candidate, out client))
                    return true;
                long area = (long)Math.Max(0, client.Width) * Math.Max(0, client.Height);
                if (area > bestArea)
                {
                    best = candidate;
                    bestArea = area;
                }
                return true;
            }, IntPtr.Zero);
            return best;
        }

        public static bool TryGetClientScreenRect(IntPtr window, out NativeRect screen)
        {
            screen = new NativeRect();
            NativeRect client;
            if (!GetClientRect(window, out client))
                return false;
            NativePoint origin = new NativePoint { X = 0, Y = 0 };
            if (!ClientToScreen(window, ref origin))
                return false;
            screen.Left = origin.X;
            screen.Top = origin.Y;
            screen.Right = origin.X + client.Width;
            screen.Bottom = origin.Y + client.Height;
            return client.Width > 0 && client.Height > 0;
        }

        public static bool ResizeClientOnce(IntPtr window, int requestedWidth, int requestedHeight)
        {
            NativeRect client;
            NativeRect outer;
            if (!GetClientRect(window, out client) || !GetWindowRect(window, out outer))
                return false;
            int width = outer.Width + requestedWidth - client.Width;
            int height = outer.Height + requestedHeight - client.Height;
            return SetWindowPos(
                window, IntPtr.Zero, 0, 0, width, height, SwpNoMove | SwpNoZOrder);
        }

        public static void BringForward(IntPtr window)
        {
            if (IsIconic(window))
                ShowWindow(window, SwRestore);
            SetWindowPos(
                window, HwndTopMost, 0, 0, 0, 0,
                SwpNoMove | SwpNoSize | SwpShowWindow);
            SetForegroundWindow(window);
        }

        public static void Minimize(IntPtr window)
        {
            ShowWindow(window, SwMinimize);
        }

        public static void Restore(IntPtr window)
        {
            ShowWindow(window, SwRestore);
            BringForward(window);
        }

        private static IntPtr Coordinates(int x, int y)
        {
            uint packed = ((uint)(ushort)y << 16) | (ushort)x;
            return new IntPtr(unchecked((int)packed));
        }

        public static bool MovePointer(IntPtr window, int x, int y)
        {
            NativePoint point = new NativePoint { X = x, Y = y };
            return ClientToScreen(window, ref point) && SetCursorPos(point.X, point.Y);
        }

        public static bool PressLeft(IntPtr window, int x, int y)
        {
            return PostMessage(
                window, WmLeftButtonDown, new IntPtr(MkLeftButton), Coordinates(x, y));
        }

        public static bool ReleaseLeft(IntPtr window, int x, int y)
        {
            return PostMessage(
                window, WmLeftButtonUp, IntPtr.Zero, Coordinates(x, y));
        }

        public static bool RequestClose(IntPtr window)
        {
            return IsWindow(window) &&
                PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
        }
    }
}
'@
}

function ConvertTo-WindowsCommandLineArgument {
    param([AllowEmptyString()][string] $Value)

    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.Replace('\', '\').Replace('"', '\"') + '"'
}

function Get-ClientRectangle {
    param([IntPtr] $WindowHandle)

    $rectangle = [BattleSpades.WindowSmoke.NativeRect]::new()
    if (-not [BattleSpades.WindowSmoke.NativeMethods]::TryGetClientScreenRect(
            $WindowHandle, [ref] $rectangle)) {
        throw 'Could not query the client rectangle.'
    }
    return $rectangle
}

function Set-ExactClientSize {
    param([IntPtr] $WindowHandle, [int] $Width, [int] $Height)

    for ($attempt = 0; $attempt -lt 12; $attempt++) {
        $current = Get-ClientRectangle -WindowHandle $WindowHandle
        if ($current.Width -eq $Width -and $current.Height -eq $Height) {
            return
        }
        if (-not [BattleSpades.WindowSmoke.NativeMethods]::ResizeClientOnce(
                $WindowHandle, $Width, $Height)) {
            throw 'SetWindowPos failed while resizing the client area.'
        }
        Start-Sleep -Milliseconds 75
    }
    $observed = Get-ClientRectangle -WindowHandle $WindowHandle
    throw "Expected ${Width}x${Height}, observed $($observed.Width)x$($observed.Height)."
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

function ConvertFrom-DesignRectangle {
    param(
        [double] $X,
        [double] $Y,
        [double] $Width,
        [double] $Height,
        [int] $ClientAreaWidth,
        [int] $ClientAreaHeight
    )

    $start = ConvertFrom-DesignPoint `
        -X $X -Y $Y -Width $ClientAreaWidth -Height $ClientAreaHeight
    $end = ConvertFrom-DesignPoint `
        -X ($X + $Width) -Y ($Y + $Height) `
        -Width $ClientAreaWidth -Height $ClientAreaHeight
    return [System.Drawing.Rectangle]::FromLTRB($start.X, $start.Y, $end.X, $end.Y)
}

function Copy-ClientBitmap {
    param([IntPtr] $WindowHandle)

    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $bitmap = [System.Drawing.Bitmap]::new($rectangle.Width, $rectangle.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $rectangle.Left, $rectangle.Top, 0, 0,
            [System.Drawing.Size]::new($rectangle.Width, $rectangle.Height),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
    }
    finally {
        $graphics.Dispose()
    }
    return $bitmap
}

function Get-MeanPixelDifference {
    param(
        [System.Drawing.Bitmap] $First,
        [System.Drawing.Bitmap] $Second,
        [System.Drawing.Rectangle] $Region
    )

    [long] $total = 0
    [long] $samples = 0
    for ($y = $Region.Top; $y -lt $Region.Bottom; $y += 2) {
        for ($x = $Region.Left; $x -lt $Region.Right; $x += 2) {
            $a = $First.GetPixel($x, $y)
            $b = $Second.GetPixel($x, $y)
            $total += [Math]::Abs([int] $a.R - [int] $b.R)
            $total += [Math]::Abs([int] $a.G - [int] $b.G)
            $total += [Math]::Abs([int] $a.B - [int] $b.B)
            $samples += 3
        }
    }
    return [double] $total / [double] [Math]::Max(1, $samples)
}

function Wait-ForCondition {
    param([scriptblock] $Condition, [int] $TimeoutMilliseconds, [string] $Failure)

    $deadline = [DateTime]::UtcNow.AddMilliseconds($TimeoutMilliseconds)
    do {
        if (& $Condition) {
            return
        }
        Start-Sleep -Milliseconds 25
    } while ([DateTime]::UtcNow -lt $deadline)
    throw $Failure
}

[BattleSpades.WindowSmoke.NativeMethods]::EnablePhysicalPixelCoordinates()

$quotedArguments = @($ArgumentList | ForEach-Object {
        ConvertTo-WindowsCommandLineArgument -Value $_
    }) -join ' '
$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $executablePath
$startInfo.Arguments = $quotedArguments
$startInfo.WorkingDirectory = $workingDirectoryPath
$startInfo.UseShellExecute = $false
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$started = $false
$windowHandle = [IntPtr]::Zero
$hoverBitmap = $null
$pressedBitmap = $null
$restoredBitmap = $null
$mouseLeaveBitmap = $null
$gracefulQuit = $false

try {
    if (-not $process.Start()) {
        throw "Failed to launch $executablePath"
    }
    $started = $true

    $deadline = [DateTime]::UtcNow.AddSeconds($WindowTimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) {
            throw "Client exited with code $($process.ExitCode) before opening its window."
        }
        $windowHandle = [BattleSpades.WindowSmoke.NativeMethods]::FindBestWindow(
            [uint32] $process.Id)
        if ($windowHandle -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($windowHandle -eq [IntPtr]::Zero) {
        throw "Timed out waiting for the client window owned by PID $($process.Id)."
    }

    [BattleSpades.WindowSmoke.NativeMethods]::BringForward($windowHandle)
    # The recovered retail Window constructor calls set_minimum_size(320, 240).
    # Ask Win32 for less and require the platform constraint to clamp it.
    if (-not [BattleSpades.WindowSmoke.NativeMethods]::ResizeClientOnce(
            $windowHandle, 160, 120)) {
        throw 'SetWindowPos failed while probing the retail minimum extent.'
    }
    Start-Sleep -Milliseconds 150
    $minimumProbe = Get-ClientRectangle -WindowHandle $windowHandle
    if ($minimumProbe.Width -lt 320 -or $minimumProbe.Height -lt 240) {
        throw "Window violated the retail 320x240 minimum: $($minimumProbe.Width)x$($minimumProbe.Height)."
    }

    Set-ExactClientSize `
        -WindowHandle $windowHandle -Width $ClientWidth -Height $ClientHeight
    [BattleSpades.WindowSmoke.NativeMethods]::BringForward($windowHandle)
    # Frontend boot now warms every integrated destination one render-thread
    # frame at a time. Do not compare a boot frame with the settled Select Menu
    # and misdiagnose that expected visual change as stuck pointer capture.
    Start-Sleep -Milliseconds 1800

    $join = ConvertFrom-DesignPoint `
        -X 400.0 -Y 208.0 -Width $ClientWidth -Height $ClientHeight
    $joinRegion = ConvertFrom-DesignRectangle `
        -X 269.0 -Y 179.0 -Width 262.0 -Height 58.0 `
        -ClientAreaWidth $ClientWidth -ClientAreaHeight $ClientHeight

    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, 8, 8)
    Start-Sleep -Milliseconds 50
    if (-not [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
            $windowHandle, $join.X, $join.Y)) {
        throw 'Failed to post Join Match mouse motion.'
    }
    Start-Sleep -Milliseconds 150
    $hoverBitmap = Copy-ClientBitmap -WindowHandle $windowHandle

    if (-not [BattleSpades.WindowSmoke.NativeMethods]::PressLeft(
            $windowHandle, $join.X, $join.Y)) {
        throw 'Failed to post Join Match mouse press.'
    }
    Start-Sleep -Milliseconds 150
    $pressedBitmap = Copy-ClientBitmap -WindowHandle $windowHandle

    $pressedDifference = Get-MeanPixelDifference `
        -First $hoverBitmap -Second $pressedBitmap -Region $joinRegion
    if ($pressedDifference -lt 0.25) {
        throw "Join Match did not render a distinct pressed state (mean delta $pressedDifference)."
    }

    # Intentionally omit mouse-up. The minimize/focus-loss path must cancel
    # the capture; otherwise this control remains visually pressed forever.
    [BattleSpades.WindowSmoke.NativeMethods]::Minimize($windowHandle)
    Wait-ForCondition `
        -Condition { [BattleSpades.WindowSmoke.NativeMethods]::IsIconic($windowHandle) } `
        -TimeoutMilliseconds 3000 `
        -Failure 'Client window did not enter the minimized state.'
    $process.Refresh()
    if ($process.HasExited) {
        throw "Client exited during minimize with code $($process.ExitCode)."
    }

    [BattleSpades.WindowSmoke.NativeMethods]::Restore($windowHandle)
    Wait-ForCondition `
        -Condition { -not [BattleSpades.WindowSmoke.NativeMethods]::IsIconic($windowHandle) } `
        -TimeoutMilliseconds 3000 `
        -Failure 'Client window did not restore.'
    $restoredRectangle = Get-ClientRectangle -WindowHandle $windowHandle
    if ($restoredRectangle.Width -ne $ClientWidth -or
        $restoredRectangle.Height -ne $ClientHeight) {
        throw "Restore changed the client area to $($restoredRectangle.Width)x$($restoredRectangle.Height)."
    }

    # Force a real SDL motion transition after restore. SetCursorPos at the
    # same coordinates may emit no WM_MOUSEMOVE, leaving the correctly
    # cancelled control in Normal rather than Hovered and producing a false
    # pixel-difference failure.
    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, 8, 8)
    Start-Sleep -Milliseconds 50
    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, $join.X, $join.Y)
    Start-Sleep -Milliseconds 200
    $restoredBitmap = Copy-ClientBitmap -WindowHandle $windowHandle
    $restoredDifference = Get-MeanPixelDifference `
        -First $hoverBitmap -Second $restoredBitmap -Region $joinRegion
    if (-not [string]::IsNullOrWhiteSpace($evidencePath)) {
        $hoverBitmap.Save(
            [System.IO.Path]::Combine($evidencePath, '01-hover.png'),
            [System.Drawing.Imaging.ImageFormat]::Png)
        $pressedBitmap.Save(
            [System.IO.Path]::Combine($evidencePath, '02-pressed.png'),
            [System.Drawing.Imaging.ImageFormat]::Png)
        $restoredBitmap.Save(
            [System.IO.Path]::Combine($evidencePath, '03-restored-hover.png'),
            [System.Drawing.Imaging.ImageFormat]::Png)
    }
    if ($restoredDifference -gt [Math]::Max(0.25, $pressedDifference * 0.25)) {
        throw "Held pointer capture survived focus loss (hover delta $restoredDifference, pressed delta $pressedDifference)."
    }

    # Repeat the capture interruption without changing keyboard focus. SDL's
    # mouse-leave event must provide the same cancellation guarantee.
    [void] [BattleSpades.WindowSmoke.NativeMethods]::PressLeft(
        $windowHandle, $join.X, $join.Y)
    Start-Sleep -Milliseconds 100
    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, $ClientWidth + 50, $ClientHeight + 50)
    Start-Sleep -Milliseconds 150
    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, $join.X, $join.Y)
    Start-Sleep -Milliseconds 150
    $mouseLeaveBitmap = Copy-ClientBitmap -WindowHandle $windowHandle
    $mouseLeaveDifference = Get-MeanPixelDifference `
        -First $hoverBitmap -Second $mouseLeaveBitmap -Region $joinRegion
    if ($mouseLeaveDifference -gt [Math]::Max(0.25, $pressedDifference * 0.25)) {
        throw "Held pointer capture survived mouse leave (hover delta $mouseLeaveDifference, pressed delta $pressedDifference)."
    }

    if (-not [string]::IsNullOrWhiteSpace($evidencePath)) {
        $mouseLeaveBitmap.Save(
            [System.IO.Path]::Combine($evidencePath, '04-mouse-leave-hover.png'),
            [System.Drawing.Imaging.ImageFormat]::Png)
    }

    $quit = ConvertFrom-DesignPoint `
        -X 400.0 -Y 555.0 -Width $ClientWidth -Height $ClientHeight
    [void] [BattleSpades.WindowSmoke.NativeMethods]::MovePointer(
        $windowHandle, $quit.X, $quit.Y)
    Start-Sleep -Milliseconds 75
    if (-not [BattleSpades.WindowSmoke.NativeMethods]::PressLeft(
            $windowHandle, $quit.X, $quit.Y) -or
        -not [BattleSpades.WindowSmoke.NativeMethods]::ReleaseLeft(
            $windowHandle, $quit.X, $quit.Y)) {
        throw 'Failed to click the Quit menu control.'
    }

    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
        throw 'Quit control did not terminate the client before the deadline.'
    }
    $gracefulQuit = $true
    if ($process.ExitCode -ne 0) {
        throw "Quit control terminated the client with exit code $($process.ExitCode)."
    }

    [pscustomobject]@{
        Result = 'PASS'
        ProcessId = $process.Id
        ClientExtent = "${ClientWidth}x${ClientHeight}"
        MinimumProbeExtent = "$($minimumProbe.Width)x$($minimumProbe.Height)"
        PressedMeanPixelDelta = [Math]::Round($pressedDifference, 3)
        RestoredHoverMeanPixelDelta = [Math]::Round($restoredDifference, 3)
        MouseLeaveHoverMeanPixelDelta = [Math]::Round($mouseLeaveDifference, 3)
        QuitExitCode = $process.ExitCode
        EvidenceDirectory = $evidencePath
    }
}
finally {
    if ($null -ne $hoverBitmap) {
        $hoverBitmap.Dispose()
    }
    if ($null -ne $pressedBitmap) {
        $pressedBitmap.Dispose()
    }
    if ($null -ne $restoredBitmap) {
        $restoredBitmap.Dispose()
    }
    if ($null -ne $mouseLeaveBitmap) {
        $mouseLeaveBitmap.Dispose()
    }
    if ($null -ne $process) {
        try {
            if ($started -and -not $gracefulQuit) {
                $process.Refresh()
                if (-not $process.HasExited) {
                    $shutdownWindow = $windowHandle
                    if ($shutdownWindow -eq [IntPtr]::Zero) {
                        $shutdownWindow = [BattleSpades.WindowSmoke.NativeMethods]::FindBestWindow(
                            [uint32] $process.Id)
                    }
                    if ($shutdownWindow -ne [IntPtr]::Zero) {
                        [void] [BattleSpades.WindowSmoke.NativeMethods]::RequestClose(
                            $shutdownWindow)
                    }
                    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
                        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
                        [void] $process.WaitForExit(5000)
                    }
                }
            }
        }
        finally {
            $process.Dispose()
        }
    }
}
