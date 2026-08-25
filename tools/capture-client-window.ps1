<#
.SYNOPSIS
Launches BattleSpadesClient and captures its exact SDL client area to PNG.

.DESCRIPTION
Starts the requested executable directly, waits for a visible top-level window
owned by that exact process ID, optionally resizes the drawable client area,
brings the window forward, and captures only the client pixels. The launched
process is always asked to close with WM_CLOSE after the capture; a forced stop
is used only if graceful shutdown exceeds the configured timeout.

The capture uses per-monitor-v2 DPI awareness so GetClientRect and
ClientToScreen coordinates address physical desktop pixels. The desktop must be
interactive and unlocked; CopyFromScreen cannot capture a disconnected or
locked Windows session.

.PARAMETER Executable
Path to the BattleSpadesClient executable to launch.

.PARAMETER OutputPath
Destination .png path. The parent directory is created when necessary and the
file is replaced atomically after a successful capture.

.PARAMETER ArgumentList
Optional client arguments. For a persistent visual-test window, pass
--run-forever when the executable supports it.

.PARAMETER WorkingDirectory
Working directory for the client. Defaults to the executable's directory.

.PARAMETER ClientWidth
Optional exact client-area width in physical pixels. ClientHeight is required
when this is set.

.PARAMETER ClientHeight
Optional exact client-area height in physical pixels. ClientWidth is required
when this is set.

.PARAMETER WindowTitle
Optional exact title filter when the process owns more than one visible
top-level window. Without it, the largest client area is selected.

.PARAMETER WindowTimeoutSeconds
Maximum time to wait for the SDL window. Defaults to 20 seconds.

.PARAMETER SettleMilliseconds
Delay after foregrounding/resizing and before capture. Defaults to 500 ms.

.PARAMETER ShutdownTimeoutSeconds
Time allowed for WM_CLOSE shutdown before Stop-Process is used. Defaults to 5
seconds.

.EXAMPLE
.\tools\capture-client-window.ps1 `
    -Executable .\out\build\dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -OutputPath .\out\screenshots\main-menu.png `
    -ArgumentList @('--run-forever') `
    -ClientWidth 1280 -ClientHeight 720

.EXAMPLE
# Syntax-only self-test; exits nonzero and prints parser errors when malformed.
powershell.exe -NoProfile -Command `
    '$e=$null;$t=$null;[System.Management.Automation.Language.Parser]::ParseFile(
      (Resolve-Path ".\tools\capture-client-window.ps1"),[ref]$t,[ref]$e)|Out-Null;
      $e|ForEach-Object{Write-Error $_};if($e.Count){exit 1}'

.NOTES
Windows only. The target window must be visible and unobscured because desktop
composition is captured, matching what a player actually sees.
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $Executable,

    [Parameter(Mandatory = $true)]
    [string] $OutputPath,

    [string[]] $ArgumentList = @(),
    [string] $WorkingDirectory = '',

    [ValidateRange(0, 16384)]
    [int] $ClientWidth = 0,

    [ValidateRange(0, 16384)]
    [int] $ClientHeight = 0,

    [string] $WindowTitle = '',

    [ValidateRange(1, 300)]
    [int] $WindowTimeoutSeconds = 20,

    [ValidateRange(0, 30000)]
    [int] $SettleMilliseconds = 500,

    [ValidateRange(1, 60)]
    [int] $ShutdownTimeoutSeconds = 5,

    [int[]] $ClickClientPoint = @(),
    [ValidateRange(0, 120000)]
    [int] $PreClickDelayMilliseconds = 0,
    [string[]] $KeySequence = @(),

    [ValidateRange(0, 30000)]
    [int] $AutomationDelayMilliseconds = 500,

    [ValidateRange(0, 5000)]
    [int] $KeyDelayMilliseconds = 100
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if ($env:OS -ne 'Windows_NT') {
    throw 'capture-client-window.ps1 requires Windows.'
}
if (($ClientWidth -eq 0) -xor ($ClientHeight -eq 0)) {
    throw 'ClientWidth and ClientHeight must either both be zero or both be specified.'
}
if ($ClientWidth -ne 0 -and ($ClientWidth -lt 64 -or $ClientHeight -lt 64)) {
    throw 'Requested client dimensions must be at least 64x64.'
}
if ($ClickClientPoint.Count -ne 0 -and $ClickClientPoint.Count -ne 2) {
    throw 'ClickClientPoint must be empty or contain exactly X,Y.'
}

$executablePath = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
if (-not [System.IO.File]::Exists($executablePath)) {
    throw "Executable is not a file: $executablePath"
}
if ([System.IO.Path]::GetExtension($OutputPath) -ine '.png') {
    throw 'OutputPath must use the .png extension.'
}

if ([System.IO.Path]::IsPathRooted($OutputPath)) {
    $capturePath = [System.IO.Path]::GetFullPath($OutputPath)
}
else {
    $capturePath = [System.IO.Path]::GetFullPath(
        [System.IO.Path]::Combine((Get-Location).Path, $OutputPath))
}

$captureDirectory = [System.IO.Path]::GetDirectoryName($capturePath)
if ([string]::IsNullOrWhiteSpace($captureDirectory)) {
    throw 'OutputPath must resolve to a parent directory.'
}
[System.IO.Directory]::CreateDirectory($captureDirectory) | Out-Null

if ([string]::IsNullOrWhiteSpace($WorkingDirectory)) {
    $workingDirectoryPath = [System.IO.Path]::GetDirectoryName($executablePath)
}
else {
    $workingDirectoryPath = (Resolve-Path -LiteralPath $WorkingDirectory -ErrorAction Stop).Path
}
if (-not [System.IO.Directory]::Exists($workingDirectoryPath)) {
    throw "WorkingDirectory is not a directory: $workingDirectoryPath"
}

if ($null -eq ('BattleSpades.VisualCapture.NativeMethods' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace BattleSpades.VisualCapture
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
        private const int SwRestore = 9;
        private const uint WmClose = 0x0010;
        private const uint WmKeyDown = 0x0100;
        private const uint WmKeyUp = 0x0101;
        private const uint WmLButtonDown = 0x0201;
        private const uint WmLButtonUp = 0x0202;
        private const int MkLButton = 0x0001;

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

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern int GetWindowText(IntPtr window, StringBuilder text, int maximumCount);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetClientRect(IntPtr window, out NativeRect rectangle);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool GetWindowRect(IntPtr window, out NativeRect rectangle);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool ClientToScreen(IntPtr window, ref NativePoint point);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetCursorPos(int x, int y);

        [DllImport("user32.dll")]
        private static extern void mouse_event(
            uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);

        [DllImport("user32.dll")]
        private static extern void keybd_event(
            byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool SetWindowPos(
            IntPtr window,
            IntPtr insertAfter,
            int x,
            int y,
            int width,
            int height,
            uint flags);

        [DllImport("user32.dll")]
        private static extern bool IsIconic(IntPtr window);

        [DllImport("user32.dll")]
        private static extern bool ShowWindow(IntPtr window, int command);

        [DllImport("user32.dll")]
        private static extern bool SetForegroundWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern IntPtr SetFocus(IntPtr window);

        [DllImport("user32.dll")]
        private static extern IntPtr SetActiveWindow(IntPtr window);

        [DllImport("user32.dll")]
        private static extern uint GetWindowThreadProcessId(IntPtr window, IntPtr processId);

        [DllImport("kernel32.dll")]
        private static extern uint GetCurrentThreadId();

        [DllImport("user32.dll")]
        private static extern bool AttachThreadInput(uint attach, uint attachTo, bool value);

        [DllImport("user32.dll")]
        private static extern uint MapVirtualKey(uint code, uint mapType);

        [DllImport("user32.dll", SetLastError = true)]
        private static extern bool PostMessage(IntPtr window, uint message, IntPtr wParam, IntPtr lParam);

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

        public static IntPtr FindBestWindow(uint requiredProcessId, string requiredTitle)
        {
            IntPtr best = IntPtr.Zero;
            long bestArea = -1;

            EnumWindows(delegate(IntPtr candidate, IntPtr ignored)
            {
                uint candidateProcessId;
                GetWindowThreadProcessId(candidate, out candidateProcessId);
                if (candidateProcessId != requiredProcessId || !IsWindowVisible(candidate))
                    return true;
                if (GetWindow(candidate, GwOwner) != IntPtr.Zero)
                    return true;

                if (!String.IsNullOrEmpty(requiredTitle))
                {
                    StringBuilder title = new StringBuilder(512);
                    GetWindowText(candidate, title, title.Capacity);
                    if (!String.Equals(title.ToString(), requiredTitle, StringComparison.Ordinal))
                        return true;
                }

                NativeRect client;
                if (!GetClientRect(candidate, out client))
                    return true;
                long width = Math.Max(0, client.Width);
                long height = Math.Max(0, client.Height);
                long area = width * height;
                if (area > bestArea)
                {
                    bestArea = area;
                    best = candidate;
                }
                return true;
            }, IntPtr.Zero);

            return best;
        }

        public static bool TryGetClientScreenRect(IntPtr window, out NativeRect screenRectangle)
        {
            screenRectangle = new NativeRect();
            NativeRect client;
            if (!GetClientRect(window, out client))
                return false;

            NativePoint origin = new NativePoint { X = client.Left, Y = client.Top };
            if (!ClientToScreen(window, ref origin))
                return false;

            screenRectangle.Left = origin.X;
            screenRectangle.Top = origin.Y;
            screenRectangle.Right = origin.X + client.Width;
            screenRectangle.Bottom = origin.Y + client.Height;
            return client.Width > 0 && client.Height > 0;
        }

        public static bool ResizeClientOnce(IntPtr window, int requestedWidth, int requestedHeight)
        {
            NativeRect client;
            NativeRect outer;
            if (!GetClientRect(window, out client) || !GetWindowRect(window, out outer))
                return false;

            int targetOuterWidth = outer.Width + (requestedWidth - client.Width);
            int targetOuterHeight = outer.Height + (requestedHeight - client.Height);
            if (targetOuterWidth <= 0 || targetOuterHeight <= 0)
                return false;

            return SetWindowPos(
                window,
                IntPtr.Zero,
                0,
                0,
                targetOuterWidth,
                targetOuterHeight,
                SwpNoMove | SwpNoZOrder);
        }

        public static void BringForward(IntPtr window)
        {
            if (IsIconic(window))
                ShowWindow(window, SwRestore);
            // Foreground activation can be denied by Windows focus-stealing
            // rules. The target is short-lived, so temporarily placing it in
            // the topmost band is deterministic and cannot outlive capture.
            SetWindowPos(
                window,
                HwndTopMost,
                0,
                0,
                0,
                0,
                SwpNoMove | SwpNoSize | SwpShowWindow);
            SetForegroundWindow(window);
        }

        public static bool RequestClose(IntPtr window)
        {
            return IsWindow(window) && PostMessage(window, WmClose, IntPtr.Zero, IntPtr.Zero);
        }

        public static bool PostClientClick(IntPtr window, int x, int y)
        {
            // Pyglet polls the physical cursor for mouse coordinates on some
            // Win32 paths, so posting WM_LBUTTON* alone can click wherever the
            // user's cursor happened to be. Move the cursor to the requested
            // client point and inject a real button transition instead.
            NativePoint screen = new NativePoint { X = x, Y = y };
            if (!ClientToScreen(window, ref screen) ||
                !SetCursorPos(screen.X, screen.Y))
                return false;
            SetForegroundWindow(window);
            System.Threading.Thread.Sleep(30);
            const uint MouseLeftDown = 0x0002;
            const uint MouseLeftUp = 0x0004;
            mouse_event(MouseLeftDown, 0, 0, 0, UIntPtr.Zero);
            System.Threading.Thread.Sleep(30);
            mouse_event(MouseLeftUp, 0, 0, 0, UIntPtr.Zero);
            return true;
        }

        public static bool PostVirtualKey(IntPtr window, int virtualKey)
        {
            if (!IsWindow(window) || virtualKey < 0 || virtualKey > 255)
                return false;
            uint scanCode = MapVirtualKey((uint)virtualKey, 0);
            // SetForegroundWindow alone may be rejected after the async
            // identity-to-menu transition. Temporarily join the SDL window's
            // input queue so the injected hardware transition has an actual
            // keyboard target instead of silently reaching the prior window.
            uint targetThread = GetWindowThreadProcessId(window, IntPtr.Zero);
            uint currentThread = GetCurrentThreadId();
            bool attached = targetThread != 0 && targetThread != currentThread &&
                            AttachThreadInput(currentThread, targetThread, true);
            SetActiveWindow(window);
            SetFocus(window);
            SetForegroundWindow(window);
            System.Threading.Thread.Sleep(20);
            // SDL3's Windows keyboard backend consumes real key transitions;
            // posted WM_KEYDOWN messages never reach its input state. Inject
            // the same hardware-style transition used by the mouse helper so
            // parity captures cannot silently remain on the previous screen.
            const uint Extended = 0x0001;
            const uint KeyUp = 0x0002;
            uint flags = virtualKey >= 0x21 && virtualKey <= 0x2e ? Extended : 0;
            keybd_event((byte)virtualKey, (byte)scanCode, flags, UIntPtr.Zero);
            System.Threading.Thread.Sleep(30);
            keybd_event((byte)virtualKey, (byte)scanCode, flags | KeyUp, UIntPtr.Zero);
            if (attached)
                AttachThreadInput(currentThread, targetThread, false);
            return true;
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

    $builder = [System.Text.StringBuilder]::new()
    [void] $builder.Append('"')
    $backslashCount = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $backslashCount++
            continue
        }
        if ($character -eq '"') {
            [void] $builder.Append(('\' * (($backslashCount * 2) + 1)))
            [void] $builder.Append('"')
            $backslashCount = 0
            continue
        }
        if ($backslashCount -gt 0) {
            [void] $builder.Append(('\' * $backslashCount))
            $backslashCount = 0
        }
        [void] $builder.Append($character)
    }
    if ($backslashCount -gt 0) {
        [void] $builder.Append(('\' * ($backslashCount * 2)))
    }
    [void] $builder.Append('"')
    return $builder.ToString()
}

function Get-ClientScreenRectangle {
    param([IntPtr] $WindowHandle)

    $rectangle = [BattleSpades.VisualCapture.NativeRect]::new()
    if (-not [BattleSpades.VisualCapture.NativeMethods]::TryGetClientScreenRect(
            $WindowHandle, [ref] $rectangle)) {
        throw "GetClientRect/ClientToScreen failed for window 0x$($WindowHandle.ToInt64().ToString('X'))."
    }
    return $rectangle
}

function Set-ExactClientSize {
    param(
        [IntPtr] $WindowHandle,
        [int] $Width,
        [int] $Height
    )

    # Window managers may apply DPI or decoration changes asynchronously. Each
    # correction is based on the newly observed client/outer delta, so no
    # hardcoded title-bar metrics are involved.
    for ($attempt = 0; $attempt -lt 12; $attempt++) {
        $current = Get-ClientScreenRectangle -WindowHandle $WindowHandle
        if ($current.Width -eq $Width -and $current.Height -eq $Height) {
            return
        }
        if (-not [BattleSpades.VisualCapture.NativeMethods]::ResizeClientOnce(
                $WindowHandle, $Width, $Height)) {
            throw "SetWindowPos failed while requesting a ${Width}x${Height} client area."
        }
        Start-Sleep -Milliseconds 75
    }

    $observed = Get-ClientScreenRectangle -WindowHandle $WindowHandle
    throw "Window refused exact ${Width}x${Height} client size; observed $($observed.Width)x$($observed.Height)."
}

function Save-ClientAreaPng {
    param(
        [IntPtr] $WindowHandle,
        [string] $DestinationPath
    )

    Add-Type -AssemblyName System.Drawing
    $rectangle = Get-ClientScreenRectangle -WindowHandle $WindowHandle
    $temporaryPath = [System.IO.Path]::Combine(
        [System.IO.Path]::GetDirectoryName($DestinationPath),
        ([System.IO.Path]::GetRandomFileName() + '.png'))

    $bitmap = $null
    $graphics = $null
    try {
        $bitmap = [System.Drawing.Bitmap]::new(
            $rectangle.Width,
            $rectangle.Height,
            [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        $graphics.CopyFromScreen(
            $rectangle.Left,
            $rectangle.Top,
            0,
            0,
            [System.Drawing.Size]::new($rectangle.Width, $rectangle.Height),
            [System.Drawing.CopyPixelOperation]::SourceCopy)
        $bitmap.Save($temporaryPath, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    catch {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        throw
    }
    finally {
        if ($null -ne $graphics) {
            $graphics.Dispose()
        }
        if ($null -ne $bitmap) {
            $bitmap.Dispose()
        }
    }

    try {
        $verification = [System.Drawing.Image]::FromFile($temporaryPath)
        try {
            if ($verification.Width -ne $rectangle.Width -or
                $verification.Height -ne $rectangle.Height) {
                throw 'PNG dimensions differ from the captured client rectangle.'
            }
        }
        finally {
            $verification.Dispose()
        }
        Move-Item -LiteralPath $temporaryPath -Destination $DestinationPath -Force
    }
    catch {
        Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        throw
    }

    return $rectangle
}

[BattleSpades.VisualCapture.NativeMethods]::EnablePhysicalPixelCoordinates()

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
$processStarted = $false
$windowHandle = [IntPtr]::Zero
$capturedRectangle = $null

try {
    if (-not $process.Start()) {
        throw "Failed to launch $executablePath"
    }
    $processStarted = $true

    $deadline = [DateTime]::UtcNow.AddSeconds($WindowTimeoutSeconds)
    do {
        $process.Refresh()
        if ($process.HasExited) {
            throw "Client exited with code $($process.ExitCode) before creating a visible SDL window."
        }

        $windowHandle = [BattleSpades.VisualCapture.NativeMethods]::FindBestWindow(
            [uint32] $process.Id, $WindowTitle)
        if ($windowHandle -ne [IntPtr]::Zero) {
            break
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($windowHandle -eq [IntPtr]::Zero) {
        $titleDetail = if ([string]::IsNullOrEmpty($WindowTitle)) {
            ''
        }
        else {
            " with title '$WindowTitle'"
        }
        throw "Timed out waiting for a visible top-level window owned by PID $($process.Id)$titleDetail."
    }

    [BattleSpades.VisualCapture.NativeMethods]::BringForward($windowHandle)
    if ($ClientWidth -ne 0) {
        Set-ExactClientSize -WindowHandle $windowHandle -Width $ClientWidth -Height $ClientHeight
        [BattleSpades.VisualCapture.NativeMethods]::BringForward($windowHandle)
    }
    if ($ClickClientPoint.Count -eq 2) {
        if ($PreClickDelayMilliseconds -gt 0) {
            Start-Sleep -Milliseconds $PreClickDelayMilliseconds
        }
        if (-not [BattleSpades.VisualCapture.NativeMethods]::PostClientClick(
                $windowHandle, $ClickClientPoint[0], $ClickClientPoint[1])) {
            throw 'Failed to post the requested client click.'
        }
        Start-Sleep -Milliseconds $AutomationDelayMilliseconds
    }
    $virtualKeys = @{
        'F10' = 0x79
        'TAB' = 0x09
        'ENTER' = 0x0D
        'SPACE' = 0x20
        'LEFT' = 0x25
        'UP' = 0x26
        'RIGHT' = 0x27
        'DOWN' = 0x28
        '1' = 0x31
        '2' = 0x32
        '3' = 0x33
    }
    foreach ($keyName in $KeySequence) {
        if ($keyName -match '^WAIT:(\d+)$') {
            $waitMilliseconds = [int] $Matches[1]
            if ($waitMilliseconds -gt 120000) {
                throw "Automation wait exceeds 120000 ms: $keyName"
            }
            Start-Sleep -Milliseconds $waitMilliseconds
            continue
        }
        if ($keyName -match '^CLICK:(\d+),(\d+)$') {
            if (-not [BattleSpades.VisualCapture.NativeMethods]::PostClientClick(
                    $windowHandle, [int] $Matches[1], [int] $Matches[2])) {
                throw "Failed to post automation click: $keyName"
            }
            Start-Sleep -Milliseconds $KeyDelayMilliseconds
            continue
        }
        $normalized = $keyName.ToUpperInvariant()
        if (-not $virtualKeys.ContainsKey($normalized)) {
            throw "Unsupported automation key: $keyName"
        }
        if (-not [BattleSpades.VisualCapture.NativeMethods]::PostVirtualKey(
                $windowHandle, $virtualKeys[$normalized])) {
            throw "Failed to post automation key: $keyName"
        }
        Start-Sleep -Milliseconds $KeyDelayMilliseconds
    }
    if ($SettleMilliseconds -gt 0) {
        Start-Sleep -Milliseconds $SettleMilliseconds
    }

    $process.Refresh()
    if ($process.HasExited) {
        throw "Client exited with code $($process.ExitCode) before capture."
    }
    $capturedRectangle = Save-ClientAreaPng `
        -WindowHandle $windowHandle `
        -DestinationPath $capturePath

    [pscustomobject]@{
        Path = $capturePath
        ProcessId = $process.Id
        WindowHandle = ('0x{0:X}' -f $windowHandle.ToInt64())
        ClientWidth = $capturedRectangle.Width
        ClientHeight = $capturedRectangle.Height
    }
}
finally {
    if ($null -ne $process) {
        try {
            if ($processStarted) {
                $process.Refresh()
                if (-not $process.HasExited) {
                    $shutdownWindow = $windowHandle
                    if ($shutdownWindow -eq [IntPtr]::Zero) {
                        # A title-filter timeout may still have created the SDL
                        # window. Find any window for the launched PID so the
                        # failure path remains graceful rather than waiting for
                        # the forced-stop fallback.
                        $shutdownWindow = [BattleSpades.VisualCapture.NativeMethods]::FindBestWindow(
                            [uint32] $process.Id, '')
                    }
                    if ($shutdownWindow -ne [IntPtr]::Zero) {
                        [void] [BattleSpades.VisualCapture.NativeMethods]::RequestClose($shutdownWindow)
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
