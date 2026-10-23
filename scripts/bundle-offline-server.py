"""Install a checksum-verified companion server beside a packaged client."""
from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import tempfile
import urllib.request
import zipfile

SERVER_VERSION = "0.3.0-beta.3"
RELEASE_URL = f"https://github.com/KikoTs/BattleSpades/releases/download/v{SERVER_VERSION}"


def install_bundle(archive: Path, checksum: str, target: Path, *, console_only: bool = False) -> None:
    """Validate an archive and preserve executable permissions while staging it."""
    with archive.open("rb") as stream:
        digest = hashlib.sha256()
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    if digest.hexdigest() != checksum.lower():
        raise ValueError(f"Server checksum mismatch: {archive.name}")
    if target.exists():
        raise FileExistsError(f"Server destination already exists: {target}")
    with tempfile.TemporaryDirectory(prefix="battlespades-server-") as temporary:
        root = Path(temporary).resolve()
        with zipfile.ZipFile(archive) as package:
            for item in package.infolist():
                destination = (root / item.filename).resolve()
                if not destination.is_relative_to(root):
                    raise ValueError("Server archive escapes its extraction directory")
                package.extract(item, root)
                if os.name != "nt" and not item.is_dir():
                    permissions = (item.external_attr >> 16) & 0o777
                    if permissions:
                        destination.chmod(permissions)
        folders = list(root.iterdir())
        if len(folders) != 1 or not folders[0].is_dir():
            raise ValueError("Expected one portable server root")
        bundle = folders[0]
        if (bundle / "VERSION").read_text(encoding="utf-8").strip() != SERVER_VERSION:
            raise ValueError("Wrong companion server version")
        for part in ("_internal", "maps", "maps/unofficial"):
            if not (bundle / part).is_dir():
                raise ValueError(f"Incomplete server: missing {part}")
        if not any((bundle / name).is_file() for name in ("BattleSpades.exe", "BattleSpades")):
            raise ValueError("Server executable is missing")
        target.parent.mkdir(parents=True, exist_ok=True)
        # The client owns the console server, tutorial and map creator. Its Mac
        # companion does not need the standalone desktop host: the Intel Tk
        # build pulls in Homebrew GUI libraries requiring macOS 14, while the
        # game and console server support macOS 11. Keep those GUI-only native
        # dependencies out of the client app; the standalone release stays full.
        gui_paths = {
            "BattleSpadesServer", "_internal/PIL", "_internal/customtkinter",
            "_internal/server_gui", "_internal/_tcl_data", "_internal/_tk_data",
            "_internal/libtk8.6.dylib", "_internal/libtcl8.6.dylib",
            "_internal/libfontconfig.dylib", "_internal/libfreetype.6.dylib",
            "_internal/libintl.8.dylib", "_internal/libpng16.16.dylib",
        }

        def ignore_gui(directory: str, names: list[str]) -> set[str]:
            relative = Path(directory).relative_to(bundle)
            return {name for name in names if (relative / name).as_posix() in gui_paths
                    or (relative.as_posix() == "_internal/lib-dynload" and name.startswith("_tkinter."))}

        shutil.copytree(bundle, target, ignore=ignore_gui if console_only else None)
        if console_only:
            (target / "README.txt").write_text(
                "BattleSpades offline companion: console server, tutorial and map creator.\n"
                "The client starts these automatically for local matches.\n"
                f"For the standalone desktop server host, download {RELEASE_URL.rsplit('/download/', 1)[0]}/tag/v{SERVER_VERSION}\n",
                encoding="utf-8",
            )


def main() -> None:
    """Download the pinned release and verify its published SHA-256."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--family", choices=("windows", "linux", "macos"), required=True)
    parser.add_argument("--architecture", choices=("x64", "arm64"), required=True)
    parser.add_argument("--target", type=Path, required=True)
    args = parser.parse_args()
    architecture = "x86_64" if args.architecture == "x64" else "arm64"
    name = f"BattleSpades-{SERVER_VERSION}-{args.family}-{architecture}.zip"
    with urllib.request.urlopen(f"{RELEASE_URL}/SHA256SUMS.txt", timeout=60) as response:
        checksums = response.read().decode("utf-8")
    matching = [line.split()[0] for line in checksums.splitlines()
                if len(line.split()) == 2 and line.split()[1].lstrip("*") == name]
    if len(matching) != 1:
        raise ValueError(f"Missing or ambiguous checksum for {name}")
    with tempfile.TemporaryDirectory(prefix="battlespades-download-") as temporary:
        archive = Path(temporary) / name
        with urllib.request.urlopen(f"{RELEASE_URL}/{name}", timeout=120) as response:
            with archive.open("wb") as output:
                shutil.copyfileobj(response, output)
        install_bundle(archive, matching[0], args.target, console_only=args.family == "macos")
    print(f"Bundled server {SERVER_VERSION}: {args.family}-{architecture}")


if __name__ == "__main__":
    main()
