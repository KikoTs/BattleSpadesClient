param(
    # bgfx shaderc built from the same source revision as the vcpkg bgfx.
    [string]$ShaderC = "$PSScriptRoot\..\out\shaderc-build\cmake\bgfx\shaderc.exe",
    [string]$BgfxInclude = "$PSScriptRoot\..\out\vcpkg\packages\bgfx_x64-windows\include\bgfx"
)

$ErrorActionPreference = "Stop"

# Flag sets reproduce the checked-in UI shader binaries byte-for-byte, so a
# recompile of an unchanged source is always a no-op diff.
$backends = @(
    @{ Name = "dx11";  Platform = "windows"; VsProfile = "s_5_0";  FsProfile = "s_5_0";  Extra = @("-O", "3") },
    @{ Name = "glsl";  Platform = "linux";   VsProfile = "130";    FsProfile = "130";    Extra = @() },
    @{ Name = "essl";  Platform = "android"; VsProfile = "300_es"; FsProfile = "300_es"; Extra = @() },
    @{ Name = "spirv"; Platform = "linux";   VsProfile = "spirv";  FsProfile = "spirv";  Extra = @() },
    @{ Name = "metal"; Platform = "osx";     VsProfile = "metal";  FsProfile = "metal";  Extra = @() }
)

$shaders = @(
    @{ Source = "vs_ui.sc";    Type = "vertex";   Output = "vs_ui.bin";    Varying = "varying.def.sc" },
    @{ Source = "fs_ui.sc";    Type = "fragment"; Output = "fs_ui.bin";    Varying = "varying.def.sc" },
    @{ Source = "vs_world.sc"; Type = "vertex";   Output = "vs_world.bin"; Varying = "varying_world.def.sc" },
    @{ Source = "fs_world.sc"; Type = "fragment"; Output = "fs_world.bin"; Varying = "varying_world.def.sc" },
    @{ Source = "vs_skydome.sc"; Type = "vertex"; Output = "vs_skydome.bin"; Varying = "varying_skydome.def.sc" },
    @{ Source = "fs_skydome.sc"; Type = "fragment"; Output = "fs_skydome.bin"; Varying = "varying_skydome.def.sc" },
    @{ Source = "vs_particle.sc"; Type = "vertex"; Output = "vs_particle.bin"; Varying = "varying_particle.def.sc" },
    @{ Source = "fs_particle.sc"; Type = "fragment"; Output = "fs_particle.bin"; Varying = "varying_particle.def.sc" },
    @{ Source = "vs_shadow.sc"; Type = "vertex";   Output = "vs_shadow.bin"; Varying = "varying_shadow.def.sc" },
    @{ Source = "fs_shadow.sc"; Type = "fragment"; Output = "fs_shadow.bin"; Varying = "varying_shadow.def.sc" }
)

$shaderSource = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\src\render\shaders"))
$outputRoots = @(
    (Join-Path $shaderSource "bin"),
    [IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\assets\generated\shaders"))
)

if (-not (Test-Path -LiteralPath $ShaderC)) {
    throw "shaderc was not found: $ShaderC"
}
if (-not (Test-Path -LiteralPath (Join-Path $BgfxInclude "bgfx_shader.sh"))) {
    throw "bgfx shader includes were not found: $BgfxInclude"
}

foreach ($backend in $backends) {
    foreach ($shader in $shaders) {
        $profile = if ($shader.Type -eq "vertex") { $backend.VsProfile } else { $backend.FsProfile }
        $primary = Join-Path (Join-Path $outputRoots[0] $backend.Name) $shader.Output
        New-Item -ItemType Directory -Force (Split-Path $primary) | Out-Null
        $arguments = @(
            "-f", (Join-Path $shaderSource $shader.Source),
            "-o", $primary,
            "--type", $shader.Type,
            "--platform", $backend.Platform,
            "-p", $profile,
            "-i", $BgfxInclude,
            "--varyingdef", (Join-Path $shaderSource $shader.Varying)
        ) + $backend.Extra
        & $ShaderC @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "shaderc failed for $($shader.Source) [$($backend.Name)]"
        }
        foreach ($root in $outputRoots[1..($outputRoots.Count - 1)]) {
            $copy = Join-Path (Join-Path $root $backend.Name) $shader.Output
            New-Item -ItemType Directory -Force (Split-Path $copy) | Out-Null
            Copy-Item -LiteralPath $primary -Destination $copy -Force
        }
        Write-Host "compiled $($backend.Name)/$($shader.Output)"
    }
}
Write-Host "All shader variants compiled."

