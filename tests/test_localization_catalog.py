"""Encoding and coverage regression tests for shipped language packs."""

from __future__ import annotations

import json
import runpy
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
CATALOG_ROOT = PROJECT_ROOT / "config" / "localization"
COMPLETE_RETAIL_LOCALES = {
    "de",
    "es",
    "es-MX",
    "fr",
    "it",
    "ja",
    "pl",
    "pt-BR",
    "ru",
    "tr",
}
CLIENT_OVERLAY_LOCALES = {
    "bg",
    "cs",
    "de",
    "es",
    "fr",
    "it",
    "ja",
    "pl",
    "pt-BR",
    "ru",
    "tr",
    "uk",
}
COMMUNITY_CORE_LOCALES = {"bg", "cs", "uk"}
COMMUNITY_CORE_KEYS = {
    "SETTINGS",
    "PLAYER_PROFILE",
    "JOIN_MATCH",
    "CREATE_MATCH",
    "WELCOME",
    "QUIT",
    "BACK",
    "CANCEL",
    "DONE",
    "FRIENDS",
    "SERVER_BROWSER",
    "MATCH_LOBBY",
    "START_GAME",
    "CHANGE_TEAM",
    "CHANGE_CLASS",
    "RESUME",
}
CLIENT_OVERLAY_KEYS = {
    "LANGUAGE",
    "LOGOUT",
    "PLAYER IDENTITY",
    "USERNAME",
    "PASSWORD",
    "SIGN IN",
    "REGISTER",
    "PLAY AS GUEST",
    "SIGN IN, REGISTER, OR PLAY AS GUEST",
    "SAVE YOUR RECOVERY CODE",
    "REQUESTS",
    "INVITES",
    "SEARCH PLAYER...",
    "ONLINE",
    "OFFLINE",
    "LOBBY INVITATIONS",
    "CREATE LOBBY",
    "DECLINE",
    "ACCEPT",
    "FRIENDS + LOBBY",
}
MAJOR_REVIVAL_LOCALES = {"bg", "de", "en", "es", "fr", "ja", "ru"}
MAJOR_CLIENT_OVERLAY_KEYS = {
    "ADD FRIEND",
    "CREATE + INVITE",
    "STARTING MATCH...",
    "WAITING FOR HOST...",
    "JOINING MATCH...",
    "RECONNECTING TO AOSPLAY...",
    "ACTION NEEDS ATTENTION",
    "LOBBY_DETAILS_UNAVAILABLE",
    "MAP_PREVIEW_UNAVAILABLE",
    "NO_LOCAL_UGC_MAPS",
    "SERVER_LOCATION",
    "IP:PORT OR HOSTNAME",
    "HOST",
    "IN_GAME",
    "ASSET_PRELOAD_FAILED",
    "COMPATIBILITY_SHADER",
    "CONTENT_REQUIRED",
    "ERROR_TIMEOUT",
    "KEEP_RESOLUTION_PROMPT",
    "LEADERBOARD_UNAVAILABLE",
    "MAP_CREATOR_LOBBY_INFO",
    "MATCHMAKING_OFFLINE",
    "MATCHMAKING_READY_TO_REFRESH",
    "MATCHMAKING_UNAVAILABLE",
    "NETWORK_UNAVAILABLE",
    "NO_LOCAL_MAPS",
    "NO_MATCH_LOBBIES_FOUND",
    "NO_UGC_LOBBIES_FOUND",
    "SEARCHING_FOR_CUSTOM_MATCH",
    "SEARCHING_FOR_MATCH_LOBBIES",
    "SEARCHING_FOR_PUBLIC_MATCH",
    "SEARCHING_FOR_UGC_LOBBIES",
    "SELECT_A_LOCAL_MAP",
    "SELECT_UGC_LOBBY",
    "SERVER_CONNECTION_FAILED",
    "SERVER_SEARCH_FAILED",
    "UGC_DELETING_MAP_MESSAGE",
    "UGC_DESCRIPTION",
    "UGC_OPERATION_ERROR",
}


def load_pack(locale: str) -> dict[str, object]:
    """Decode one pack strictly as UTF-8 and return its JSON object."""

    data = (CATALOG_ROOT / f"{locale}.json").read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        raise AssertionError(f"{locale}.json must not contain a UTF-8 BOM")
    return json.loads(data.decode("utf-8", errors="strict"))


class LocalizationCatalogTests(unittest.TestCase):
    """Guard complete recovered translations and Unicode metadata."""

    def test_complete_retail_languages_cover_all_retail_keys(self) -> None:
        english = load_pack("en")["strings"]
        self.assertIsInstance(english, dict)
        self.assertGreaterEqual(len(english), 1_700)
        retail_keys = set(english) - CLIENT_OVERLAY_KEYS - MAJOR_CLIENT_OVERLAY_KEYS

        for locale in COMPLETE_RETAIL_LOCALES:
            with self.subTest(locale=locale):
                strings = load_pack(locale)["strings"]
                missing = retail_keys - strings.keys()
                self.assertFalse(missing, f"{locale} is missing keys: {sorted(missing)}")
                changed = sum(strings[key] != english[key] for key in retail_keys)
                self.assertGreater(changed, 1_000)

    def test_appearance_and_discord_settings_survive_catalog_regeneration_in_every_locale(self) -> None:
        generator_path = PROJECT_ROOT / "tools" / "generate-localization-catalog.py"
        generator = runpy.run_path(generator_path)
        overlays = {
            locale: {**appearance, **generator["DISCORD_SETTINGS_OVERLAYS"][locale]}
            for locale, appearance in generator["APPEARANCE_SETTINGS_OVERLAYS"].items()
        }
        self.assertEqual(set(overlays), set(generator["LOCALES"]))
        keys = {"SHOW_SKINS", "SHOW_OTHER_SKINS", "WEAPON_MOTION", "DISCORD_PRESENCE",
                "DISCORD_PRESENCE_DESCRIPTION", "DISCORD_JOIN", "DISCORD_JOIN_DESCRIPTION"}
        english = load_pack("en")["strings"]
        for locale, overlay in overlays.items():
            with self.subTest(locale=locale):
                self.assertEqual(set(overlay), keys)
                strings = load_pack(locale)["strings"]
                self.assertEqual({key: strings[key] for key in keys}, overlay)
                if locale != "en":
                    self.assertTrue(all(strings[key] != english[key] for key in keys))

        # Exercise the real generation entry point without depending on an
        # installed retail depot or rewriting any editable shipped pack.
        with tempfile.TemporaryDirectory(prefix="aos-localization-") as temporary:
            source = Path(temporary) / "source"
            output = Path(temporary) / "generated"
            source.mkdir()
            for metadata in generator["LOCALES"].values():
                if metadata.source_module is not None:
                    (source / metadata.source_module).write_text(
                        'RETAIL_SENTINEL = "preserved retail text"\n', encoding="utf-8"
                    )
            subprocess.run(
                [sys.executable, str(generator_path), "--source", str(source),
                 "--output-dir", str(output)],
                check=True,
                capture_output=True,
                text=True,
                encoding="utf-8",
            )
            for locale, overlay in overlays.items():
                with self.subTest(regenerated_locale=locale):
                    document = json.loads(
                        (output / f"{locale}.json").read_text(encoding="utf-8")
                    )
                    strings = document["strings"]
                    self.assertEqual({key: strings[key] for key in keys}, overlay)
                    if generator["LOCALES"][locale].source_module is not None:
                        self.assertEqual(strings["RETAIL_SENTINEL"], "preserved retail text")

    def test_revival_controls_are_localized_in_initial_major_languages(self) -> None:
        english = load_pack("en")["strings"]
        for locale in CLIENT_OVERLAY_LOCALES:
            with self.subTest(locale=locale):
                strings = load_pack(locale)["strings"]
                self.assertTrue(CLIENT_OVERLAY_KEYS.issubset(strings))
                translated = sum(
                    strings[key] != english[key] for key in CLIENT_OVERLAY_KEYS
                )
                # Names such as "Online" and "Password" are legitimately
                # identical in a few European languages.
                self.assertGreaterEqual(translated, len(CLIENT_OVERLAY_KEYS) - 2)

    def test_community_starter_packs_cover_the_core_navigation(self) -> None:
        english = load_pack("en")["strings"]
        for locale in COMMUNITY_CORE_LOCALES:
            with self.subTest(locale=locale):
                strings = load_pack(locale)["strings"]
                self.assertTrue(COMMUNITY_CORE_KEYS.issubset(strings))
                self.assertTrue(
                    all(strings[key] != english[key] for key in COMMUNITY_CORE_KEYS)
                )

    def test_major_languages_cover_native_friends_lobby_and_hosting_states(self) -> None:
        english = load_pack("en")["strings"]
        for locale in MAJOR_REVIVAL_LOCALES:
            with self.subTest(locale=locale):
                strings = load_pack(locale)["strings"]
                self.assertTrue(MAJOR_CLIENT_OVERLAY_KEYS.issubset(strings))
                if locale != "en":
                    translated = sum(
                        strings[key] != english[key]
                        for key in MAJOR_CLIENT_OVERLAY_KEYS
                    )
                    # Product names and short technical labels such as Host
                    # legitimately remain identical in some languages.
                    self.assertGreaterEqual(translated, len(MAJOR_CLIENT_OVERLAY_KEYS) - 2)

    def test_no_pack_contains_replacement_characters_or_mojibake(self) -> None:
        for path in CATALOG_ROOT.glob("*.json"):
            with self.subTest(path=path.name):
                document = load_pack(path.stem)
                values = [document["native_name"], *document["strings"].values()]
                self.assertTrue(all("\ufffd" not in value for value in values))
                self.assertTrue(all("Ã" not in value and "Ð" not in value for value in values))
                self.assertTrue(
                    all(
                        not any(
                            value[index - 1].isalpha()
                            and value[index] == "?"
                            and value[index + 1].isalpha()
                            for index in range(1, len(value) - 1)
                        )
                        for value in values
                    ),
                    "question marks embedded inside words indicate code-page loss",
                )

    def test_script_specific_faces_use_owned_retail_assets(self) -> None:
        self.assertEqual(load_pack("ru")["font_asset"], "fonts/Tuffy_Bold.ttf")
        self.assertEqual(load_pack("pl")["font_asset"], "fonts/Tuffy_Bold.ttf")
        self.assertEqual(load_pack("tr")["font_asset"], "fonts/Tuffy_Bold.ttf")
        self.assertEqual(
            load_pack("ja")["font_asset"], "fonts/NotoSansJP-SemiBold.ttf"
        )
        self.assertEqual(load_pack("ru")["native_name"], "Русский")
        self.assertEqual(load_pack("ja")["native_name"], "日本語")

    def test_japanese_pack_repairs_retail_simplified_chinese_typos(self) -> None:
        strings = load_pack("ja")["strings"]
        forbidden = set("弹阅尝悬标仓")
        self.assertFalse(
            forbidden.intersection("".join(strings.values())),
            "Japanese strings must not retain glyphs absent from the retail JP face",
        )
        self.assertEqual(strings["UGC_MENU_SUBSCRIBE"], "購読")
        self.assertEqual(strings["SUBSCRIBED_MAPS"], "購読済みマップ")

    def test_japanese_pack_is_not_the_cyrillic_polluted_depot_copy(self) -> None:
        strings = load_pack("ja")["strings"]
        polluted = [
            key
            for key, value in strings.items()
            if any("\u0400" <= character <= "\u04ff" for character in value)
        ]
        self.assertEqual(polluted, [])
        self.assertEqual(strings["A2362"], "クラシック")

    def test_japanese_font_is_bundled_with_its_open_font_license(self) -> None:
        font_root = PROJECT_ROOT / "assets" / "client" / "fonts"
        font = font_root / "NotoSansJP-SemiBold.ttf"
        license_file = font_root / "OFL-NotoSansJP.txt"
        self.assertTrue(font.is_file())
        self.assertGreater(font.stat().st_size, 1_000_000)
        self.assertIn("SIL OPEN FONT LICENSE", license_file.read_text(encoding="utf-8"))


if __name__ == "__main__":
    unittest.main()
