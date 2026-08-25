"""Regression tests for the retail asset-import catalog policy."""

from __future__ import annotations

import json
import runpy
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
ASSET_TOOL = runpy.run_path(str(PROJECT_ROOT / "tools" / "assets.py"), run_name="asset_tool")


class AssetToolPolicyTests(unittest.TestCase):
    def test_implementation_and_editable_sources_are_not_runtime_requirements(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            allowed = ASSET_TOOL["ASSET_DIRECTORY_EXTENSIONS"]
            for directory, extensions in allowed.items():
                target = root / directory
                target.mkdir(parents=True)
                extension = sorted(extensions)[0]
                (target / f"fixture{extension}").write_bytes(b"asset")

            (root / "list.pnq").write_bytes(b"root")
            (root / "playlists" / "__init__.pyc").write_bytes(b"bytecode")
            (root / "playlists" / "mapinfo.pyc").write_bytes(b"bytecode")
            (root / "tga" / "editable.pdn").write_bytes(b"source")
            (root / "kv6" / "editable.vox").write_bytes(b"source")

            assets = ASSET_TOOL["discover_source_assets"](root)
            paths = {asset.relative.as_posix() for asset in assets}
            self.assertNotIn("playlists/__init__.pyc", paths)
            self.assertNotIn("playlists/mapinfo.pyc", paths)
            self.assertNotIn("tga/editable.pdn", paths)
            self.assertNotIn("kv6/editable.vox", paths)
            self.assertIn("list.pnq", paths)

    def test_checked_in_manifest_contains_only_stock_runtime_assets(self) -> None:
        manifest = json.loads(
            (PROJECT_ROOT / "assets" / "catalog" / "original-assets.json").read_text(
                encoding="utf-8"
            )
        )
        paths = {entry["path"] for entry in manifest["files"]}
        forbidden = {
            "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf",
            "fonts/NotoSansJP-SemiBold.ttf",
            "kv6/snowblower.vox",
            "playlists/__init__.pyc",
            "playlists/mapinfo.pyc",
            "tga/ao_cube512.pdn",
        }
        self.assertTrue(paths.isdisjoint(forbidden))
        self.assertEqual(manifest["file_count"], len(manifest["files"]))
        self.assertEqual(
            manifest["total_bytes"],
            sum(entry["size"] for entry in manifest["files"]),
        )


if __name__ == "__main__":
    unittest.main()
