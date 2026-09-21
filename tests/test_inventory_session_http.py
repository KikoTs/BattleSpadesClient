"""Exercise atomic equips, one-response collection updates and a stale revision."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import threading
import time
from urllib.parse import unquote

CATALOG = Path(__file__).resolve().parents[1] / "src/frontend/inventory_catalog.inc"
DIGEST = re.search(r'inventory_catalog_digest = "([0-9a-f]+)"', CATALOG.read_text()).group(1)
VERSION = "collection-v5"
LEGACY_DIGEST = "aa008150d68e0df9925ffa354b944a06010698bbac62d16c497f29581ae7ac3e"
ITEM = "community-stg44-v2"
RECEIPT = json.loads((CATALOG.parents[2] / "tests/fixtures/inventory-receipts-v2.json").read_text())[4]


class Handler(BaseHTTPRequestHandler):
    revision = 2
    equipped: dict[str, str] = {}
    writes: list[tuple[str, str, int]] = []
    injected_conflict = False
    injected_unavailable = False
    reads = 0
    open_requests: list[dict] = []
    opened = False

    def log_message(self, *_: object) -> None:
        pass

    def respond(self) -> None:
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
        status = 200
        if self.path == "/api/auth/login":
            result = {"account": {"public_id": "inventory-delay-fixture", "legacy_id": "1000", "nickname": "Fixture",
                                 "account_type": "registered", "identity_type": "account"}, "access_token": "fixture-token"}
        else:
            assert self.headers.get("Authorization") == "Bearer fixture-token"
            time.sleep(0.18 if self.command == "GET" else 0.28)
            if self.command == "GET":
                Handler.reads += 1
                result = {"schema_version": 1, "catalog": {"version": VERSION, "digest": DIGEST},
                          "progression": {"level": "2", "lifetime_xp": "1100", "level_xp": "100", "xp_to_next": "1250",
                                          "inventory_revision": str(Handler.revision), "unopened_crates": "1", "pity": [0, 0, 0],
                                          "features": {"opening": True, "equip": True, "awards": True}},
                          "inventory": {"items": [{"cosmetic_id": ITEM}]},
                          "equipped": [{"slot": slot, "cosmetic_id": item} for slot, item in Handler.equipped.items()],
                          "crates": {"items": [{"id": RECEIPT["crate_id"], "level": "12", "catalog_version": RECEIPT["catalog_version"], "commitment": RECEIPT["commitment"]}], "next_cursor": None}, "history": {"items": [], "next_cursor": None}}
                if not Handler.injected_unavailable:
                    Handler.injected_unavailable = True
                    status = 503
                    result = {"error": "collection_unavailable", "message": "Injected service outage"}
            elif self.path.endswith("/open"):
                Handler.open_requests.append(body)
                if not Handler.opened:
                    Handler.opened = True
                    Handler.revision += 1
                    self.close_connection = True  # Commit succeeds, reply is lost.
                    return
                assert body == Handler.open_requests[0], "Opening retry changed its nonce or idempotency key"
                result = {**RECEIPT, "idempotency_key": body["idempotency_key"], "collection": {
                    "schema_version": 1, "catalog": {"version": VERSION, "digest": DIGEST},
                    "progression": {"level": "2", "lifetime_xp": "1100", "level_xp": "100", "xp_to_next": "1250",
                        "inventory_revision": str(Handler.revision), "unopened_crates": "0", "pity": [0, 0, 0],
                        "features": {"opening": True, "equip": True, "awards": True}},
                    "inventory": {"items": [{"cosmetic_id": ITEM}, {"cosmetic_id": RECEIPT["item"]["id"]}]},
                    "equipped": [{"slot": s, "cosmetic_id": i} for s, i in Handler.equipped.items()],
                    "crates": {"items": [], "next_cursor": None}, "history": {"items": [{"receipt": RECEIPT}], "next_cursor": None}}}
            else:
                slot = unquote(self.path.rsplit("/", 1)[-1])
                assert slot in ("weapon:60:view", "weapon:60:world")
                assert int(body["expected_inventory_revision"]) == Handler.revision
                Handler.writes.append((self.command, slot, Handler.revision))
                if self.command == "DELETE" and not Handler.injected_conflict:
                    Handler.injected_conflict = True
                    Handler.revision += 1
                    status = 409
                    result = {"error": "inventory_conflict", "message": "Injected concurrent change"}
                else:
                    assert body["include_collection"] is True
                    for target in ("weapon:60:view", "weapon:60:world"):
                        if self.command == "PUT": Handler.equipped[target] = ITEM
                        else: Handler.equipped.pop(target, None)
                    Handler.revision += 1
                    if self.command == "PUT" and Handler.revision == 6:
                        self.close_connection = True  # Equip saved; verify without writing again.
                        return
                    result = {"schema_version": 1, "inventory_revision": str(Handler.revision), "collection": {
                        "schema_version": 1, "catalog": {"version": VERSION, "digest": DIGEST},
                        "progression": {"level": "2", "lifetime_xp": "1100", "level_xp": "100", "xp_to_next": "1250",
                            "inventory_revision": str(Handler.revision), "unopened_crates": "0", "pity": [0, 0, 0],
                            "features": {"opening": True, "equip": True, "awards": True}},
                        "inventory": {"items": [{"cosmetic_id": ITEM}]},
                        "equipped": [{"slot": s, "cosmetic_id": i} for s, i in Handler.equipped.items()],
                        "crates": {"items": [{"id": RECEIPT["crate_id"], "level": "12", "catalog_version": RECEIPT["catalog_version"], "commitment": RECEIPT["commitment"]}], "next_cursor": None}, "history": {"items": [], "next_cursor": None}}}
        encoded = json.dumps(result).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    do_GET = do_POST = do_PUT = do_DELETE = respond


class AccountSwitchHandler(BaseHTTPRequestHandler):
    """Release a committed equip only after the client has switched accounts."""

    directory: Path
    calls: list[tuple[str, str]] = []

    def log_message(self, *_: object) -> None:
        pass

    def respond(self) -> None:
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
        if self.path == "/api/auth/login":
            owner = body["username"] == "OwnerFixture"
            result = {"account": {"public_id": "inventory-owner-fixture" if owner else "inventory-other-fixture",
                "legacy_id": "1001" if owner else "1002", "nickname": body["username"],
                "account_type": "registered", "identity_type": "account"},
                "access_token": "owner-token" if owner else "other-token"}
        else:
            token = self.headers.get("Authorization", "")
            AccountSwitchHandler.calls.append((self.command, token))
            if self.command == "PUT":
                assert token == "Bearer owner-token", "Old-account equip reached the new account"
                (self.directory / "mutation-started").write_text("ready", encoding="utf-8")
                deadline = time.monotonic() + 6
                while not (self.directory / "release-mutation").exists() and time.monotonic() < deadline:
                    time.sleep(0.01)
                assert (self.directory / "release-mutation").exists(), "Client never switched accounts"
                self.close_connection = True  # Commit succeeded, recovery must remain owner-bound.
                return
            assert self.command == "GET"
            result = {"schema_version": 1, "catalog": {"version": VERSION, "digest": DIGEST},
                "progression": {"level": "2", "lifetime_xp": "1100", "level_xp": "100", "xp_to_next": "1250",
                    "inventory_revision": "2" if token == "Bearer owner-token" else "21",
                    "unopened_crates": "0", "pity": [0, 0, 0],
                    "features": {"opening": True, "equip": True, "awards": True}},
                "inventory": {"items": [{"cosmetic_id": ITEM}]}, "equipped": [],
                "crates": {"items": [], "next_cursor": None}, "history": {"items": [], "next_cursor": None}}
        encoded = json.dumps(result).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    do_GET = do_POST = do_PUT = respond


with tempfile.TemporaryDirectory(prefix="aos-inventory-delay-") as directory:
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        for VERSION, DIGEST in ((VERSION, DIGEST), ("collection-v4", "45860af613b2926b20dd959855d1b4351260a3c61c693cab3fd3ae05a10b50bc"), ("collection-v2", LEGACY_DIGEST)):
            Handler.revision = 2
            Handler.equipped = {}
            Handler.writes = []
            Handler.injected_conflict = False
            Handler.injected_unavailable = False
            Handler.reads = 0
            Handler.open_requests = []
            Handler.opened = False
            state_dir = str(Path(directory) / VERSION)
            environment = {**os.environ, "AOS_REVIVAL_STATE_PATH": str(Path(state_dir) / "identity.json")}
            subprocess.run([sys.argv[1], "--session-http", f"http://127.0.0.1:{server.server_port}", state_dir],
                           env=environment, check=True, timeout=40)
            assert [revision for _, _, revision in Handler.writes] == [2, 3, 4, 5], Handler.writes
            assert Handler.reads == 5, "Unexpected reads beyond load/retry, conflict, lost equip reply and cancellation fixture"
            assert len(Handler.open_requests) == 2 and Handler.revision == 7, "Retry awarded or spent twice"
            assert len(Handler.equipped) == 2
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
    AccountSwitchHandler.directory = Path(directory) / "account-switch"
    AccountSwitchHandler.directory.mkdir()
    server = ThreadingHTTPServer(("127.0.0.1", 0), AccountSwitchHandler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        state_dir = AccountSwitchHandler.directory
        environment = {**os.environ, "AOS_REVIVAL_STATE_PATH": str(state_dir / "identity.json")}
        subprocess.run([sys.argv[1], "--account-switch-http", f"http://127.0.0.1:{server.server_port}", str(state_dir)],
                       env=environment, check=True, timeout=25)
        assert AccountSwitchHandler.calls == [("GET", "Bearer owner-token"), ("PUT", "Bearer owner-token"),
                                             ("GET", "Bearer other-token")], AccountSwitchHandler.calls
        digest = hashlib.sha256(b"inventory-owner-fixture").hexdigest()
        cached = json.loads((state_dir / "collection-cache" / f"{digest}.json").read_text())
        assert cached["progression"]["inventory_revision"] == "2", "New account response overwrote the old account cache"
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
print("Current and published legacy snapshots, offline browsing, paired equips and conflict recovery passed")
