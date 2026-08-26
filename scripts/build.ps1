param(
    [ValidateSet("Debug", "Dev", "Release")]
    [string]$Profile = "Dev",
    [switch]$Native,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

$projectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$preset = $Profile.ToLowerInvariant()
if ($Native) {
    # The owned retail steam_api.dll is 32-bit. Build its architecture-matched
    # hidden bridge before entering the x64 client compiler environment.
    $bridgeConfiguration = if ($Profile -eq "Release") { "Release" } else { "RelWithDebInfo" }
    & (Join-Path $PSScriptRoot "build-steam-bridge.ps1") `
        -Configuration $bridgeConfiguration
    if ($LASTEXITCODE -ne 0) {
        throw "Steam bridge build failed for $Profile."
    }

    # Activate one compiler environment before both vcpkg and CMake run. This
    # prevents libraries from being built by a newer Visual Studio than the
    # client executable when several installations coexist.
    $requestedVcpkgRoot = $env:VCPKG_ROOT
    $vswhere = Join-Path ${env:ProgramFiles(x86)} `
        "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw "Visual Studio Installer's vswhere.exe was not found."
    }

    $visualStudio = (& $vswhere `
        -latest `
        -products "*" `
        -version "[17.0,)" `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath).Trim()
    if (-not $visualStudio) {
        throw "Visual Studio 2022 or newer with the x64 C++ tools is required."
    }

    $devCommand = Join-Path $visualStudio "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $devCommand)) {
        throw "Visual Studio developer environment was not found: $devCommand"
    }

    $environmentDump = & cmd.exe /d /c `
        "call `"$devCommand`" -arch=amd64 -host_arch=amd64 >nul && set"
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to activate the Visual Studio C++ environment."
    }
    foreach ($line in $environmentDump) {
        $separator = $line.IndexOf("=")
        if ($separator -gt 0) {
            $name = $line.Substring(0, $separator)
            $value = $line.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable($name, $value, "Process")
        }
    }

    # CMake's Ninja generator persists the absolute compiler path, while
    # INCLUDE/LIB come from this process. Confirm that both halves of the
    # toolchain now belong to the same Visual Studio installation before a
    # configure or build can mix incompatible STL headers and compiler bits.
    $compiler = (& where.exe cl.exe | Select-Object -First 1)
    if (-not $compiler) {
        throw "cl.exe was not found after activating $visualStudio."
    }
    $compiler = [IO.Path]::GetFullPath($compiler.Trim())
    $visualStudioRoot = [IO.Path]::GetFullPath($visualStudio).TrimEnd(
        [IO.Path]::DirectorySeparatorChar,
        [IO.Path]::AltDirectorySeparatorChar
    ) + [IO.Path]::DirectorySeparatorChar
    if (-not $compiler.StartsWith(
        $visualStudioRoot,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw (
            "Activated compiler '$compiler' is outside the selected " +
            "Visual Studio installation '$visualStudio'."
        )
    }

    if (-not $env:VCToolsInstallDir) {
        throw "VsDevCmd did not define VCToolsInstallDir."
    }
    $vcToolsRoot = [IO.Path]::GetFullPath($env:VCToolsInstallDir)
    if (-not $vcToolsRoot.StartsWith(
        $visualStudioRoot,
        [StringComparison]::OrdinalIgnoreCase
    )) {
        throw (
            "VCToolsInstallDir '$vcToolsRoot' does not match the selected " +
            "Visual Studio installation '$visualStudio'."
        )
    }

    foreach ($variableName in @("INCLUDE", "LIB", "LIBPATH")) {
        $variableValue = [Environment]::GetEnvironmentVariable(
            $variableName,
            "Process"
        )
        foreach ($entry in ($variableValue -split ";")) {
            if ([String]::IsNullOrWhiteSpace($entry)) {
                continue
            }
            $candidate = [IO.Path]::GetFullPath($entry.Trim())
            $isVisualStudioPath = $candidate.IndexOf(
                "\Microsoft Visual Studio\",
                [StringComparison]::OrdinalIgnoreCase
            ) -ge 0
            if ($isVisualStudioPath -and -not $candidate.StartsWith(
                $visualStudioRoot,
                [StringComparison]::OrdinalIgnoreCase
            )) {
                throw (
                    "$variableName contains '$candidate' from another " +
                    "Visual Studio installation; selected '$visualStudio'."
                )
            }
        }
    }

    Write-Host "Using native compiler: $compiler"

    # VsDevCmd may inject Visual Studio's private vcpkg checkout. Presets must
    # continue to use the caller-selected, pinned checkout instead; otherwise
    # a configure can silently switch registries and invalidate the shared
    # installed tree.
    if ($requestedVcpkgRoot) {
        [Environment]::SetEnvironmentVariable(
            "VCPKG_ROOT",
            $requestedVcpkgRoot,
            "Process"
        )
    }

    $preset = "native-$preset"
}

Push-Location $projectRoot
try {
    $configureArguments = @("--preset", $preset)
    if ($Native) {
        # A Ninja cache records an absolute cl.exe. Fresh configuration is
        # required after a Visual Studio update/switch so that compiler,
        # INCLUDE, LIB, vcpkg ABI detection, and redistributables stay atomic.
        $configureArguments = @("--fresh") + $configureArguments
    }
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed for $Profile."
    }

    & cmake --build --preset $preset --parallel
    if ($LASTEXITCODE -ne 0) {
        throw "CMake build failed for $Profile."
    }

    if (-not $SkipTests) {
        & ctest --preset $preset
        if ($LASTEXITCODE -ne 0) {
            throw "CTest failed for $Profile."
        }
    }

    if ($Profile -eq "Release") {
        $buildRoot = Join-Path $projectRoot "out\build\$preset"
        $installRoot = Join-Path $projectRoot "out\install\$preset"
        & cmake --install $buildRoot --config Release --prefix $installRoot
        if ($LASTEXITCODE -ne 0) {
            throw "Release install failed."
        }
    }
} finally {
    Pop-Location
}
