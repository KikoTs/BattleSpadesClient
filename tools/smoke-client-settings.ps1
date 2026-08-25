<#
.SYNOPSIS
Exercises the retail-parity Settings flow in the real SDL client window.

.DESCRIPTION
Stages the executable and its runtime DLLs in an isolated directory, launches
the graphical client, navigates Main/Graphics/Controls with real Win32 pointer
messages, captures each screen, changes Invert Mouse, confirms with Done, and
verifies the resulting settings.toml. The staged process is always closed.

The desktop must be interactive and unlocked because this script validates the
pixels and input path that a player actually uses.

.EXAMPLE
.\tools\smoke-client-settings.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -EvidenceDirectory .\out\evidence\native-settings
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string] $EvidenceDirectory,

    [switch] $ExerciseResolutionRollback,

    [switch] $UseExecutableInPlace,

    [ValidateRange(1, 60)]
    [int] $WindowTimeoutSeconds = 20,

    [ValidateRange(1, 20)]
    [int] $ShutdownTimeoutSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'smoke-client-settings.ps1 requires Windows.'
}

$sourceExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$sourceDirectory = [System.IO.Path]::GetDirectoryName($sourceExecutable)
$evidencePath = if ([System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
    [System.IO.Path]::GetFullPath($EvidenceDirectory)
}
else {
    [System.IO.Path]::GetFullPath(
        [System.IO.Path]::Combine((Get-Location).Path, $EvidenceDirectory))
}
[System.IO.Directory]::CreateDirectory($evidencePath) | Out-Null

# Settings live next to the executable. A private runtime copy makes developer
# testing repeatable. Package validation may opt into an already isolated full
# extraction so adjacent assets and shaders are exercised exactly as shipped.
if ($UseExecutableInPlace) {
    $stagePath = $sourceDirectory
    $stagedExecutable = $sourceExecutable
}
else {
    $stagePath = [System.IO.Path]::Combine($evidencePath, 'isolated-runtime')
    [System.IO.Directory]::CreateDirectory($stagePath) | Out-Null
    $stagedExecutable = [System.IO.Path]::Combine(
        $stagePath, [System.IO.Path]::GetFileName($sourceExecutable))
    Copy-Item -LiteralPath $sourceExecutable -Destination $stagedExecutable -Force
    Get-ChildItem -LiteralPath $sourceDirectory -Filter '*.dll' -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stagePath -Force
    }
}
$settingsPath = [System.IO.Path]::Combine($stagePath, 'settings.toml')
if ([System.IO.File]::Exists($settingsPath)) {
    if ($UseExecutableInPlace) {
        throw "In-place Settings smoke refuses to replace existing file: $settingsPath"
    }
    Remove-Item -LiteralPath $settingsPath -Force
}

Add-Type -AssemblyName System.Drawing

if ($null -eq ('BattleSpades.SettingsSmoke.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

namespace BattleSpades.SettingsSmoke
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
        private const uint SwpNoMove = 0x0002;
        private const uint SwpNoZOrder = 0x0004;
        private const uint SwpShowWindow = 0x0040;
        private const uint WmClose = 0x0010;
        private const uint WmMouseWheel = 0x020A;
        private const uint WmLeftButtonDown = 0x0201;
        private const uint WmLeftButtonUp = 0x0202;
        private const uint WmMiddleButtonDown = 0x0207;
        private const uint WmMiddleButtonUp = 0x0208;
        private const uint MkLeftButton = 0x0001;
        private const uint MkMiddleButton = 0x0010;
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
            return SetWindowPos(window, IntPtr.Zero, 0, 0, width, height,
                                SwpNoMove | SwpNoZOrder);
        }

        public static void BringForward(IntPtr window)
        {
            SetWindowPos(window, HwndTopMost, 0, 0, 0, 0,
                         SwpNoMove | 0x0001 | SwpShowWindow);
            SetForegroundWindow(window);
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

        public static bool ClickLeft(IntPtr window, int x, int y)
        {
            IntPtr coordinates = Coordinates(x, y);
            if (!PostMessage(window, WmLeftButtonDown,
                             new IntPtr(MkLeftButton), coordinates))
                return false;
            System.Threading.Thread.Sleep(30);
            return PostMessage(window, WmLeftButtonUp, IntPtr.Zero, coordinates);
        }

        public static bool ClickMiddle(IntPtr window, int x, int y)
        {
            IntPtr coordinates = Coordinates(x, y);
            if (!PostMessage(window, WmMiddleButtonDown,
                             new IntPtr(MkMiddleButton), coordinates))
                return false;
            System.Threading.Thread.Sleep(30);
            return PostMessage(window, WmMiddleButtonUp, IntPtr.Zero, coordinates);
        }

        public static bool Wheel(IntPtr window, int x, int y, int detents)
        {
            NativePoint screen = new NativePoint { X = x, Y = y };
            if (!ClientToScreen(window, ref screen) || !MovePointer(window, x, y))
                return false;
            int delta = detents * 120;
            IntPtr wParam = new IntPtr(unchecked(delta << 16));
            return PostMessage(window, WmMouseWheel, wParam, Coordinates(screen.X, screen.Y));
        }

        public static bool RequestClose(IntPtr window)
        {
            return IsWindow(window) && PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
        }
    }
}
'@
}

function Get-ClientRectangle {
    param([IntPtr] $WindowHandle)
    $rectangle = [BattleSpades.SettingsSmoke.NativeRect]::new()
    if (-not [BattleSpades.SettingsSmoke.NativeMethods]::TryGetClientScreenRect(
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
        if (-not [BattleSpades.SettingsSmoke.NativeMethods]::ResizeClientOnce(
                $WindowHandle, $Width, $Height)) {
            throw 'SetWindowPos failed while resizing the client area.'
        }
        Start-Sleep -Milliseconds 75
    }
    $observed = Get-ClientRectangle -WindowHandle $WindowHandle
    throw "Expected ${Width}x${Height}, observed $($observed.Width)x$($observed.Height)."
}

function Invoke-Click {
    param([IntPtr] $WindowHandle, [int] $X, [int] $Y)
    if (-not [BattleSpades.SettingsSmoke.NativeMethods]::MovePointer(
            $WindowHandle, $X, $Y)) {
        throw "Failed to move the pointer to client point ($X, $Y)."
    }
    # SDL's Win32 backend updates its mouse state from the real cursor. Give
    # the WM_MOUSEMOVE generated by SetCursorPos time to precede button input.
    Start-Sleep -Milliseconds 90
    if (-not [BattleSpades.SettingsSmoke.NativeMethods]::ClickLeft(
            $WindowHandle, $X, $Y)) {
        throw "Failed to click client point ($X, $Y)."
    }
    Start-Sleep -Milliseconds 320
}

function Invoke-MiddleClick {
    param([IntPtr] $WindowHandle, [int] $X, [int] $Y)
    if (-not [BattleSpades.SettingsSmoke.NativeMethods]::MovePointer(
            $WindowHandle, $X, $Y)) {
        throw "Failed to move the pointer to client point ($X, $Y)."
    }
    Start-Sleep -Milliseconds 90
    if (-not [BattleSpades.SettingsSmoke.NativeMethods]::ClickMiddle(
            $WindowHandle, $X, $Y)) {
        throw "Failed to middle-click client point ($X, $Y)."
    }
    Start-Sleep -Milliseconds 320
}

function Invoke-DesignClick {
    param([IntPtr] $WindowHandle, [double] $X, [double] $Y)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    $scale = [Math]::Min($rectangle.Width / 800.0, $rectangle.Height / 600.0)
    $left = ($rectangle.Width - (800.0 * $scale)) / 2.0
    $top = ($rectangle.Height - (600.0 * $scale)) / 2.0
    Invoke-Click -WindowHandle $WindowHandle `
        -X ([int] [Math]::Round($left + ($X * $scale))) `
        -Y ([int] [Math]::Round($top + ($Y * $scale)))
}

function Save-ClientCapture {
    param([IntPtr] $WindowHandle, [string] $Name)
    $rectangle = Get-ClientRectangle -WindowHandle $WindowHandle
    if ($rectangle.Width -ne 800 -or $rectangle.Height -ne 600) {
        throw "Capture extent changed to $($rectangle.Width)x$($rectangle.Height)."
    }
    $bitmap = [System.Drawing.Bitmap]::new(800, 600)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $rectangle.Left, $rectangle.Top, 0, 0,
            [System.Drawing.Size]::new(800, 600),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
        $path = [System.IO.Path]::Combine($evidencePath, $Name)
        if ([System.IO.File]::Exists($path)) {
            [System.IO.File]::Delete($path)
        }
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Save-CurrentClientCapture {
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
        if ([System.IO.File]::Exists($path)) {
            [System.IO.File]::Delete($path)
        }
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

[BattleSpades.SettingsSmoke.NativeMethods]::EnablePhysicalPixelCoordinates()

$startInfo = [System.Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $stagedExecutable
$startInfo.Arguments = '--run-forever'
$startInfo.WorkingDirectory = $stagePath
$startInfo.UseShellExecute = $false
$process = [System.Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$windowHandle = [IntPtr]::Zero
$gracefulExit = $false

try {
    if (-not $process.Start()) {
        throw "Failed to launch $stagedExecutable"
    }

    $deadline = [DateTime]::UtcNow.AddSeconds($WindowTimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) {
            throw "Client exited with code $($process.ExitCode) before opening its window."
        }
        $windowHandle = [BattleSpades.SettingsSmoke.NativeMethods]::FindBestWindow(
            [uint32] $process.Id)
        if ($windowHandle -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)
    if ($windowHandle -eq [IntPtr]::Zero) {
        throw "Timed out waiting for the client window owned by PID $($process.Id)."
    }

    Set-ExactClientSize -WindowHandle $windowHandle -Width 800 -Height 600
    [BattleSpades.SettingsSmoke.NativeMethods]::BringForward($windowHandle)
    Start-Sleep -Milliseconds 350

    Save-ClientCapture -WindowHandle $windowHandle -Name '00-select-menu.png'

    # Select Menu wrench -> Settings/Main.
    Invoke-Click -WindowHandle $windowHandle -X 502 -Y 476
    Save-ClientCapture -WindowHandle $windowHandle -Name '01-settings-main.png'

    # Graphics top and bottom prove all options and the scroll path render.
    Invoke-Click -WindowHandle $windowHandle -X 398 -Y 116
    Save-ClientCapture -WindowHandle $windowHandle -Name '02-settings-graphics-top.png'
    Invoke-Click -WindowHandle $windowHandle -X 500 -Y 159
    Save-ClientCapture -WindowHandle $windowHandle -Name '03-settings-resolution-dropdown.png'
    if ($ExerciseResolutionRollback) {
        # The dropdown starts at the current mode. Choose its next displayed
        # resolution, apply it, and activate the visibly rendered Revert
        # button using design-space mapping after the window changes size.
        Invoke-Click -WindowHandle $windowHandle -X 450 -Y 208
        Invoke-Click -WindowHandle $windowHandle -X 525 -Y 522
        $resizeDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $changedExtent = Get-ClientRectangle -WindowHandle $windowHandle
            if ($changedExtent.Width -ne 800 -or $changedExtent.Height -ne 600) {
                break
            }
            Start-Sleep -Milliseconds 50
        } while ([DateTime]::UtcNow -lt $resizeDeadline)
        if ($changedExtent.Width -eq 800 -and $changedExtent.Height -eq 600) {
            throw 'Resolution selection did not resize the real SDL window.'
        }
        Save-CurrentClientCapture `
            -WindowHandle $windowHandle -Name '03a-resolution-confirmation.png'
        Invoke-DesignClick -WindowHandle $windowHandle -X 520 -Y 461
        $restoreDeadline = [DateTime]::UtcNow.AddSeconds(5)
        do {
            $restoredExtent = Get-ClientRectangle -WindowHandle $windowHandle
            if ($restoredExtent.Width -eq 800 -and $restoredExtent.Height -eq 600) {
                break
            }
            Start-Sleep -Milliseconds 50
        } while ([DateTime]::UtcNow -lt $restoreDeadline)
        if ($restoredExtent.Width -ne 800 -or $restoredExtent.Height -ne 600) {
            throw "Revert left the window at $($restoredExtent.Width)x$($restoredExtent.Height)."
        }
        Start-Sleep -Milliseconds 250
        Save-ClientCapture `
            -WindowHandle $windowHandle -Name '03b-resolution-restored.png'
    }
    else {
        Invoke-Click -WindowHandle $windowHandle -X 200 -Y 400
    }
    Invoke-Click -WindowHandle $windowHandle -X 625 -Y 405
    Save-ClientCapture -WindowHandle $windowHandle -Name '04-settings-graphics-bottom.png'

    # Retail orders Map Creator before Main Game. Collapse the first category
    # to expose Main Game and then exercise a real raw mouse binding.
    Invoke-Click -WindowHandle $windowHandle -X 560 -Y 116
    Save-ClientCapture -WindowHandle $windowHandle -Name '05-settings-controls-map-creator.png'
    Invoke-Click -WindowHandle $windowHandle -X 380 -Y 155
    Save-ClientCapture -WindowHandle $windowHandle -Name '06-settings-controls-main.png'
    # A configurable row enters raw-input capture on release. Bind Forward to
    # middle mouse to prove the native SDL event reaches the settings model.
    Invoke-Click -WindowHandle $windowHandle -X 470 -Y 249
    Invoke-MiddleClick -WindowHandle $windowHandle -X 470 -Y 249
    Save-ClientCapture -WindowHandle $windowHandle -Name '07-settings-mouse-binding.png'

    # Persist one deterministic Main option, then prove it survives reopening.
    Invoke-Click -WindowHandle $windowHandle -X 235 -Y 116
    Invoke-Click -WindowHandle $windowHandle -X 500 -Y 261
    Invoke-Click -WindowHandle $windowHandle -X 525 -Y 522
    if (-not [System.IO.File]::Exists($settingsPath)) {
        throw 'Done did not create settings.toml beside the staged executable.'
    }
    $persisted = [System.IO.File]::ReadAllText($settingsPath)
    if ($persisted -notmatch '(?m)^invert_mouse\s*=\s*true\s*$') {
        throw 'Done did not persist the changed Invert Mouse value.'
    }
    if ($persisted -notmatch '(?m)^forward\s*=\s*"mouse:middle"\s*$') {
        throw 'Native mouse-button capture did not persist the Forward binding.'
    }

    Invoke-Click -WindowHandle $windowHandle -X 502 -Y 476
    Save-ClientCapture -WindowHandle $windowHandle -Name '08-settings-persisted.png'
    Invoke-Click -WindowHandle $windowHandle -X 272 -Y 522

    # Exercise the menu-owned quit path rather than terminating the process.
    Invoke-Click -WindowHandle $windowHandle -X 400 -Y 555
    if (-not $process.WaitForExit($ShutdownTimeoutSeconds * 1000)) {
        throw 'Client did not exit after the visible Quit control was activated.'
    }
    $gracefulExit = $true
    if ($process.ExitCode -ne 0) {
        throw "Client quit with exit code $($process.ExitCode)."
    }

    [pscustomobject]@{
        Result = 'PASS'
        ProcessId = $process.Id
        ExitCode = $process.ExitCode
        SettingsPath = $settingsPath
        EvidenceDirectory = $evidencePath
        Captures = 9
    }
}
finally {
    if ($process -ne $null) {
        $process.Refresh()
        if (-not $process.HasExited) {
            if ($windowHandle -ne [IntPtr]::Zero) {
                [void] [BattleSpades.SettingsSmoke.NativeMethods]::RequestClose($windowHandle)
                [void] $process.WaitForExit($ShutdownTimeoutSeconds * 1000)
            }
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                [void] $process.WaitForExit()
            }
        }
        $process.Dispose()
    }
    if (-not $gracefulExit) {
        Write-Verbose 'Settings smoke cleaned up the client after a failed assertion.'
    }
}
