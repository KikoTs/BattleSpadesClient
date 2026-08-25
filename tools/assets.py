#!/usr/bin/env python3
"""Synchronize and verify the preserved Ace of Spades asset pack.

The native client consumes the original relative paths, but the source runtime
also contains Python 2 binaries, account state, logs, and platform DLLs.  This
tool copies only the immutable content roots required by the new client and
writes a deterministic SHA-256 catalog.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_DESTINATION = PROJECT_ROOT / "assets" / "original"
DEFAULT_MANIFEST = PROJECT_ROOT / "assets" / "catalog" / "original-assets.json"

# Preserve these trees byte-for-byte.  Some legacy metadata intentionally uses
# extensions such as .pyc, .txtc, .pdn, and .pnq; filtering by extension would
# silently discard information needed while reconstructing the loaders.
ASSET_DIRECTORIES = (
    "ambients",
    "fonts",
    "kv6",
    "maps",
    "mesh",
    "music",
    "playlists",
    "png",
    "prefabs",
    "skins",
    "sounds",
    "tga",
    "ugc",
)
ROOT_ASSETS = ("list.pnq",)


@dataclass(frozen=True)
class SourceAsset:
    source: Path
    relative: Path
    size: int
    sha256: str


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def normalized_relative(path: Path) -> str:
    value = path.as_posix()
    if path.is_absolute() or ".." in path.parts:
        raise ValueError(f"Unsafe asset path: {path}")
    return value


def discover_source_assets(source_root: Path) -> list[SourceAsset]:
    if not source_root.is_dir():
        raise FileNotFoundError(f"Asset source root does not exist: {source_root}")

    candidates: list[tuple[Path, Path]] = []
    for directory_name in ASSET_DIRECTORIES:
        source_directory = source_root / directory_name
        if not source_directory.is_dir():
            raise FileNotFoundError(
                f"Required asset directory is missing: {source_directory}"
            )
        for source in sorted(source_directory.rglob("*")):
            if source.is_symlink():
                raise ValueError(f"Asset source cannot contain symlinks: {source}")
            if source.is_file():
                candidates.append(
                    (source, Path(directory_name) / source.relative_to(source_directory))
                )

    for filename in ROOT_ASSETS:
        source = source_root / filename
        if not source.is_file():
            raise FileNotFoundError(f"Required root asset is missing: {source}")
        candidates.append((source, Path(filename)))

    casefolded: dict[str, Path] = {}
    assets: list[SourceAsset] = []
    for source, relative in sorted(candidates, key=lambda pair: pair[1].as_posix()):
        relative_text = normalized_relative(relative)
        collision_key = relative_text.casefold()
        previous = casefolded.get(collision_key)
        if previous is not None:
            raise ValueError(
                f"Case-insensitive asset collision: {previous} and {relative}"
            )
        casefolded[collision_key] = relative
        assets.append(
            SourceAsset(
                source=source,
                relative=relative,
                size=source.stat().st_size,
                sha256=sha256_file(source),
            )
        )
    return assets


def destination_files(destination: Path) -> set[Path]:
    if not destination.exists():
        return set()
    files: set[Path] = set()
    for path in destination.rglob("*"):
        if path.is_symlink():
            raise ValueError(f"Asset destination cannot contain symlinks: {path}")
        if path.is_file():
            files.add(path.relative_to(destination))
    return files


def assert_safe_destination(destination: Path) -> Path:
    resolved = destination.resolve()
    expected_parent = (PROJECT_ROOT / "assets").resolve()
    if resolved == expected_parent or expected_parent not in resolved.parents:
        raise ValueError(
            "Asset destination must remain below the project's assets directory: "
            f"{resolved}"
        )
    return resolved


def sync_assets(
    assets: Iterable[SourceAsset],
    destination: Path,
    *,
    prune: bool,
) -> tuple[int, int]:
    destination = assert_safe_destination(destination)
    destination.mkdir(parents=True, exist_ok=True)
    # Reject a pre-existing junction/symlink before creating any target. This
    # prevents an apparently in-tree asset path from redirecting a copy outside
    # the project.
    existing_files = destination_files(destination)
    copied = 0
    unchanged = 0
    expected: set[Path] = set()

    for asset in assets:
        expected.add(asset.relative)
        target = destination / asset.relative
        resolved_target = target.resolve()
        if destination not in resolved_target.parents:
            raise ValueError(f"Refusing redirected asset target: {target}")
        target.parent.mkdir(parents=True, exist_ok=True)
        if (
            target.is_file()
            and target.stat().st_size == asset.size
            and sha256_file(target) == asset.sha256
        ):
            unchanged += 1
            continue
        shutil.copy2(asset.source, target)
        if target.stat().st_size != asset.size or sha256_file(target) != asset.sha256:
            raise OSError(f"Copied asset failed verification: {target}")
        copied += 1

    extras = sorted(existing_files - expected)
    if extras and not prune:
        preview = ", ".join(path.as_posix() for path in extras[:8])
        raise RuntimeError(
            f"Destination contains {len(extras)} uncatalogued files ({preview}). "
            "Run sync with --prune only after reviewing them."
        )
    if prune:
        for relative in extras:
            target = (destination / relative).resolve()
            if destination not in target.parents:
                raise ValueError(f"Refusing unsafe asset removal: {target}")
            target.unlink()
        for directory in sorted(
            (path for path in destination.rglob("*") if path.is_dir()),
            key=lambda path: len(path.parts),
            reverse=True,
        ):
            try:
                directory.rmdir()
            except OSError:
                pass

    return copied, unchanged


def build_manifest(assets: Iterable[SourceAsset]) -> dict[str, object]:
    entries = [
        {
            "path": normalized_relative(asset.relative),
            "size": asset.size,
            "sha256": asset.sha256,
        }
        for asset in assets
    ]
    return {
        "schema": 1,
        "description": "Preserved Ace of Spades Battle Builder client assets",
        "file_count": len(entries),
        "total_bytes": sum(entry["size"] for entry in entries),
        "files": entries,
    }


def write_manifest(manifest: dict[str, object], path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def load_manifest(path: Path) -> dict[str, object]:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"Cannot read asset manifest {path}: {error}") from error
    if document.get("schema") != 1 or not isinstance(document.get("files"), list):
        raise ValueError(f"Unsupported asset manifest: {path}")
    return document


def verify_manifest(destination: Path, manifest_path: Path) -> None:
    destination = assert_safe_destination(destination)
    manifest = load_manifest(manifest_path)
    expected: set[Path] = set()
    total_bytes = 0
    for entry in manifest["files"]:
        if not isinstance(entry, dict):
            raise ValueError("Malformed asset manifest entry")
        relative = Path(str(entry["path"]))
        normalized_relative(relative)
        target = destination / relative
        if not target.is_file():
            raise FileNotFoundError(f"Catalogued asset is missing: {target}")
        size = target.stat().st_size
        if size != int(entry["size"]):
            raise ValueError(f"Asset size mismatch: {target}")
        if sha256_file(target) != str(entry["sha256"]):
            raise ValueError(f"Asset hash mismatch: {target}")
        expected.add(relative)
        total_bytes += size

    # The runtime catalog is a required retail subset, not an ownership claim
    # over the player's whole installation. Uncatalogued Workshop maps and
    # local compatibility files are allowed and are never copied by the
    # packaged importer.
    if len(expected) != int(manifest["file_count"]):
        raise ValueError("Asset manifest file count is inconsistent")
    if total_bytes != int(manifest["total_bytes"]):
        raise ValueError("Asset manifest byte count is inconsistent")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    subparsers = result.add_subparsers(dest="command", required=True)

    sync = subparsers.add_parser("sync", help="copy and catalog original assets")
    sync.add_argument("--source", type=Path, required=True)
    sync.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    sync.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    sync.add_argument(
        "--prune",
        action="store_true",
        help="remove uncatalogued files below the destination after copying",
    )

    verify = subparsers.add_parser("verify", help="verify the local asset catalog")
    verify.add_argument("--destination", type=Path, default=DEFAULT_DESTINATION)
    verify.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    return result


def main() -> int:
    arguments = parser().parse_args()
    try:
        if arguments.command == "sync":
            assets = discover_source_assets(arguments.source.resolve())
            copied, unchanged = sync_assets(
                assets,
                arguments.destination,
                prune=arguments.prune,
            )
            manifest = build_manifest(assets)
            write_manifest(manifest, arguments.manifest)
            verify_manifest(arguments.destination, arguments.manifest)
            print(
                "Asset sync complete: "
                f"{manifest['file_count']} files, {manifest['total_bytes']} bytes; "
                f"copied={copied}, unchanged={unchanged}"
            )
        else:
            verify_manifest(arguments.destination, arguments.manifest)
            document = load_manifest(arguments.manifest)
            print(
                "Asset verification passed: "
                f"{document['file_count']} files, {document['total_bytes']} bytes"
            )
    except (OSError, RuntimeError, ValueError) as error:
        print(f"asset tool failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
