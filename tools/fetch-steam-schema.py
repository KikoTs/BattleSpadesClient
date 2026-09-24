#!/usr/bin/env python3
"""Report the retail application's achievement and statistic schema.

The achievement *names* are public and tools/generate-steam-achievements.py
copies them. What each one requires is not: that lives in the schema, behind a
Steam Web API key. The same call returns the statistic names, without which
set_statistic() can only guess and Steam refuses every guess.

    STEAM_WEB_API_KEY=... python3 tools/fetch-steam-schema.py

A key is free from https://steamcommunity.com/dev/apikey. Whether an ordinary
key is enough for an application someone else owns is worth finding out from the
answer rather than from the documentation: a 403 means a publisher key is
required, and that route is closed to us for 224540.

The output is read by a person, not compiled. Wiring an achievement is still a
deliberate act: a description tells you what to count, and the counting has to
be something the client actually observes.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request

APP_ID = 224540


def main() -> int:
    key = os.environ.get("STEAM_WEB_API_KEY", "").strip()
    if not key:
        print(
            "STEAM_WEB_API_KEY is not set. Get one from "
            "https://steamcommunity.com/dev/apikey and run:\n"
            "  STEAM_WEB_API_KEY=... python3 tools/fetch-steam-schema.py",
            file=sys.stderr,
        )
        return 2
    url = (
        "https://api.steampowered.com/ISteamUserStats/GetSchemaForGame/v2/"
        f"?key={key}&appid={APP_ID}"
    )
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            payload = json.load(response)
    except urllib.error.HTTPError as error:
        if error.code == 403:
            print(
                "Steam refused the key for this application (403). Reading the "
                "schema of an application you do not own needs its publisher "
                "key, so the criteria stay unknown and no achievement should be "
                "wired on a guess.",
                file=sys.stderr,
            )
        else:
            print(f"Steam answered {error.code}: {error.reason}", file=sys.stderr)
        return 1

    stats = payload.get("game", {}).get("availableGameStats", {})
    achievements = stats.get("achievements", [])
    counters = stats.get("stats", [])

    print(f"achievements: {len(achievements)}")
    for entry in achievements:
        name = entry.get("name", "?")
        display = entry.get("displayName", "")
        description = entry.get("description", "")
        print(f"  {name}")
        if display:
            print(f"      {display}")
        if description:
            print(f"      {description}")
    print(f"\nstatistics: {len(counters)}")
    for entry in counters:
        print(f"  {entry.get('name', '?')}  default={entry.get('defaultvalue', 0)}")
    if not counters:
        print("  (none published, so there is nothing for set_statistic to write)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
