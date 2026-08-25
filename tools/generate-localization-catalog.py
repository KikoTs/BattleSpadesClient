#!/usr/bin/env python3
"""Build the editable client localization catalogue from recovered retail keys.

The retail decompile contains one complete English module.  The other dumped
language modules are byte-for-byte English placeholders, so this generator
does not mislabel them as authentic translations.  Community locale blocks
start with a useful menu vocabulary and safely inherit every missing key from
English at runtime.
"""

from __future__ import annotations

import argparse
import ast
import json
from pathlib import Path


LOCALES = {
    "en": ("English", ""),
    "bg": ("Български", "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf"),
    "ru": ("Русский", "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf"),
    "uk": ("Українська", "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf"),
    "pl": ("Polski", "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf"),
    "cs": ("Čeština", "fonts/Gen_Shin_Gothic_Monospace_Bold.ttf"),
    "de": ("Deutsch", ""),
    "fr": ("Français", ""),
    "es": ("Español", ""),
    "es-MX": ("Español (México)", ""),
    "pt-BR": ("Português (Brasil)", ""),
    "it": ("Italiano", ""),
    "tr": ("Türkçe", ""),
    "ja": ("日本語", "fonts/NotoSansJP-SemiBold.ttf"),
    "ko": ("한국어", "fonts/NotoSansJP-SemiBold.ttf"),
    "zh-Hans": ("简体中文", "fonts/NotoSansJP-SemiBold.ttf"),
    "zh-Hant": ("繁體中文", "fonts/NotoSansJP-SemiBold.ttf"),
}


COMMON = {
    "bg": {
        "SETTINGS": "Настройки", "PLAYER_PROFILE": "Профил", "PLAY_ONLINE": "Избери мач",
        "JOIN_MATCH": "Присъедини се", "CREATE_MATCH": "Създай мач", "WELCOME": "Добре дошъл",
        "QUIT": "Изход", "BACK": "Назад", "SELECT": "Избери", "CANCEL": "Отказ",
        "DONE": "Готово", "DEFAULTS": "По подразбиране", "FRIENDS": "Приятели",
        "SERVER_BROWSER": "Списък със сървъри", "MATCH_LOBBY": "Лоби за мач",
        "MATCH_SETTINGS": "Настройки на мача", "START_GAME": "Стартирай играта",
        "CHANGE_TEAM": "Смени отбор", "CHANGE_CLASS": "Смени клас", "RESUME": "Продължи",
    },
    "ru": {
        "SETTINGS": "Настройки", "PLAYER_PROFILE": "Профиль игрока", "PLAY_ONLINE": "Выбрать матч",
        "JOIN_MATCH": "Присоединиться", "CREATE_MATCH": "Создать матч", "WELCOME": "Добро пожаловать",
        "QUIT": "Выход", "BACK": "Назад", "SELECT": "Выбрать", "CANCEL": "Отмена",
        "DONE": "Готово", "DEFAULTS": "По умолчанию", "FRIENDS": "Друзья",
        "SERVER_BROWSER": "Список серверов", "MATCH_LOBBY": "Лобби матча",
        "MATCH_SETTINGS": "Настройки матча", "START_GAME": "Начать игру",
        "CHANGE_TEAM": "Сменить команду", "CHANGE_CLASS": "Сменить класс", "RESUME": "Продолжить",
    },
    "ja": {
        "SETTINGS": "設定", "PLAYER_PROFILE": "プレイヤープロフィール", "PLAY_ONLINE": "マッチを選択",
        "JOIN_MATCH": "マッチに参加", "CREATE_MATCH": "マッチを作成", "WELCOME": "ようこそ",
        "QUIT": "終了", "BACK": "戻る", "SELECT": "選択", "CANCEL": "キャンセル",
        "DONE": "完了", "DEFAULTS": "初期設定", "FRIENDS": "フレンド",
        "SERVER_BROWSER": "サーバーブラウザー", "MATCH_LOBBY": "マッチロビー",
        "MATCH_SETTINGS": "マッチ設定", "START_GAME": "ゲーム開始",
        "CHANGE_TEAM": "チーム変更", "CHANGE_CLASS": "クラス変更", "RESUME": "再開",
    },
    "zh-Hans": {
        "SETTINGS": "设置", "PLAYER_PROFILE": "玩家资料", "PLAY_ONLINE": "选择比赛",
        "JOIN_MATCH": "加入比赛", "CREATE_MATCH": "创建比赛", "WELCOME": "欢迎",
        "QUIT": "退出", "BACK": "返回", "SELECT": "选择", "CANCEL": "取消",
        "DONE": "完成", "DEFAULTS": "默认", "FRIENDS": "好友",
        "SERVER_BROWSER": "服务器浏览器", "MATCH_LOBBY": "比赛大厅",
        "MATCH_SETTINGS": "比赛设置", "START_GAME": "开始游戏",
        "CHANGE_TEAM": "更换队伍", "CHANGE_CLASS": "更换职业", "RESUME": "继续",
    },
    "zh-Hant": {
        "SETTINGS": "設定", "PLAYER_PROFILE": "玩家資料", "PLAY_ONLINE": "選擇比賽",
        "JOIN_MATCH": "加入比賽", "CREATE_MATCH": "建立比賽", "WELCOME": "歡迎",
        "QUIT": "退出", "BACK": "返回", "SELECT": "選擇", "CANCEL": "取消",
        "DONE": "完成", "DEFAULTS": "預設", "FRIENDS": "好友",
        "SERVER_BROWSER": "伺服器瀏覽器", "MATCH_LOBBY": "比賽大廳",
        "MATCH_SETTINGS": "比賽設定", "START_GAME": "開始遊戲",
        "CHANGE_TEAM": "更換隊伍", "CHANGE_CLASS": "更換職業", "RESUME": "繼續",
    },
}


def recovered_english(path: Path) -> dict[str, str]:
    module = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
    result: dict[str, str] = {}
    for statement in module.body:
        if not isinstance(statement, ast.Assign) or len(statement.targets) != 1:
            continue
        target = statement.targets[0]
        if not isinstance(target, ast.Name) or not target.id.isupper():
            continue
        try:
            value = ast.literal_eval(statement.value)
        except (ValueError, TypeError):
            continue
        if isinstance(value, str):
            result[target.id] = value
    return dict(sorted(result.items()))


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    english = recovered_english(args.source)
    locales = {}
    for locale, (native_name, font_asset) in LOCALES.items():
        locales[locale] = {
            "native_name": native_name,
            "font_asset": font_asset,
            "strings": english if locale == "en" else COMMON.get(locale, {}),
        }
    document = {
        "schema_version": 1,
        "active_locale": "en",
        "fallback_locale": "en",
        "_instructions": (
            "UTF-8. Change active_locale or add/edit keys under locales. "
            "Missing keys safely fall back to English; restart is not required."
        ),
        "locales": locales,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(document, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(f"wrote {args.output} with {len(english)} recovered English keys and {len(locales)} locales")


if __name__ == "__main__":
    main()
