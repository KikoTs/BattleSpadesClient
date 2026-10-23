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


def install_bundle(archive: Path, checksum: str, target: Path) -> None:
    """Validate an archive and preserve executable permissions while staging it."""
    with archive.open("rb") as stream:
        actual = hashlib.file_digest(stream, "sha256").hexdigest()
    if actual != checksum.lower():
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
        shutil.copytree(bundle, target)


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
        install_bundle(archive, matching[0], args.target)
    print(f"Bundled server {SERVER_VERSION}: {args.family}-{architecture}")


if __name__ == "__main__":
    main()
