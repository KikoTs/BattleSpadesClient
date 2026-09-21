# Select Menu visual baseline

> **Recovery/specification reference.** Preserve the measured retail behavior and its evidence. Implementation updates, old build paths, test counts and session constraints below describe their original investigation; they are not current release or deployment status. Use the [maintained documentation index](../README.md) for present operating instructions and recheck historical findings against current source.

The preserved client was launched directly into its frontend from
`G:\AoSRevival\aos-nonsteam\build\exe.win32-2.7\aos.exe` with `+s +debug`.
The client area was captured after the Select Menu settled and while no control
was hovered.

Local comparison images are intentionally kept under the ignored `out` tree:

| Client size | Local evidence | SHA-256 |
| --- | --- | --- |
| 800x600 | `out/evidence/retail-main-menu-client-800x600.png` | `CDE35AE197E1DF0176DF0A04296CEB472C4D49201F65E65FEC7053E0E628D84B` |
| 1680x1050 | `out/evidence/retail-main-menu-client-1680x1050.png` | `18DC2A1965231C0731CD9CDAFDFFA8CE7D85504DC6A5477B839671E0105E9A44` |

The accepted native captures are:

| Client size | Local evidence | SHA-256 | RGB SSIM | Minimum |
| --- | --- | --- | --- | --- |
| 800x600 | `out/evidence/native-main-menu-800x600-final.png` | `6D33AE536D8AAC7C0B064A70DA6DCA6B7C2AC4F66D53FE06D8322EC766146742` | `0.985400` | `0.98` |
| 1680x1050 | `out/evidence/native-main-menu-1680x1050-final.png` | `BEBE4FF28F3961B78C0F77BC83789FEE3889032A0201D363BBE09CD0435DB543` | `0.967606` | `0.96` |

The 800x600 frame is the primary pixel baseline. It confirms:

- `ugc_splash.png` is the active background in this target build;
- the Select Menu frame is centered at the recovered design coordinates;
- the logo extends above and overlaps the frame;
- the visible labels resolve to Join Match, Create Match, Player Profile, Map
  Creator, and Quit;
- the four lower icons are Tutorial, Achievements, Leaderboards, and Settings;
- the welcome/name plate is independently anchored to the top-right safe edge;
- the background uses cover scaling while the UI canvas uses contained scaling;
- the original uses nearest filtering on buttons/icons and linear filtering on
  the background/frame artwork.

## Reproduction

Run the capture on an interactive, unlocked Windows desktop. The helper owns
the launched process, enforces the physical client extent, captures only the
client area, and requests graceful shutdown afterward:

```powershell
.\tools\capture-client-window.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\native-main-menu-800x600-final.png `
    -WorkingDirectory . -ClientWidth 800 -ClientHeight 600 `
    -WindowTitle 'Ace of Spades'

.\tools\capture-client-window.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -OutputPath .\out\evidence\native-main-menu-1680x1050-final.png `
    -WorkingDirectory . -ClientWidth 1680 -ClientHeight 1050 `
    -WindowTitle 'Ace of Spades'
```

Use ffmpeg-backed full-frame RGB SSIM for the acceptance decision and retain
the difference frames for inspection:

```powershell
.\tools\compare-client-captures.ps1 `
    -Reference .\out\evidence\retail-main-menu-client-800x600.png `
    -Candidate .\out\evidence\native-main-menu-800x600-final.png `
    -DifferencePath .\out\evidence\native-vs-retail-difference-800-final.png `
    -MinimumSsim 0.98

.\tools\compare-client-captures.ps1 `
    -Reference .\out\evidence\retail-main-menu-client-1680x1050.png `
    -Candidate .\out\evidence\native-main-menu-1680x1050-final.png `
    -DifferencePath .\out\evidence\native-vs-retail-difference-1680-final.png `
    -MinimumSsim 0.96
```

The interactive window gate is independent of the idle pixel comparison:

```powershell
.\tools\smoke-client-window-input.ps1 `
    -Executable .\out\build\native-dev\src\RelWithDebInfo\BattleSpadesClient.exe `
    -WorkingDirectory . `
    -EvidenceDirectory .\out\platform-check\window-input-final `
    -ClientWidth 1000 -ClientHeight 700
```

It passes exact 1000x700 sizing, the recovered 320x240 minimum,
minimize/restore, focus-loss and mouse-leave capture cancellation, and graceful
Quit. The accepted scope is the static idle Select Menu and native window/input
lifecycle. It does not accept the destination screens, session actions, or a
playable client. A changed golden requires a documented retail capture or an
intentional profile-gated deviation.
