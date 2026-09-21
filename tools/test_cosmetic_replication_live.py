"""Two real ENet peers and a portable game server; all HTTP stays on loopback."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from urllib.parse import parse_qs, urlsplit

NATIVE = "ply_" + "N" * 16
LEGACY = "ply_" + "L" * 16


class Master(BaseHTTPRequestHandler):
    both_reads = 0
    def log_message(self, *_args):
        pass

    def reply(self, value):
        data = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_POST(self):
        print("Master POST", self.path, flush=True)
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        if self.path == "/api/master/auth/consume-ticket":
            native = body["ticket"] == "~" + "N" * 14
            assert native or body["ticket"] == "~" + "L" * 14
            self.reply({"authenticated": True, "player": {
                "public_id": NATIVE if native else LEGACY,
                "legacy_id": "10001" if native else "10002",
                "nickname": "NativeSkin" if native else "RetailSkin",
                "account_type": "registered", "identity_type": "password", "ranked_eligible": False,
                "client_capabilities": ["battlespades-cosmetics-v1"] if native else []}})
        else:
            self.reply({"accepted": True})

    def do_GET(self):
        print("Master GET", self.path, flush=True)
        parsed = urlsplit(self.path)
        assert parsed.path == "/api/cosmetics/equipped"
        ids = parse_qs(parsed.query)["player_id"]
        if NATIVE in ids and LEGACY in ids:
            Master.both_reads += 1
        # Real delayed HTTP: ENet simulation and both input loops must continue.
        time.sleep(1)
        self.reply({"schema_version": 1, "players": [{"player_id": identity, "items":
            ([{"slot": "weapon:6:world", "cosmetic_id": "community-lee-enfield-v2"}]
             if Master.both_reads < 2 else []) if identity == NATIVE else
            [{"slot": "weapon:60:world", "cosmetic_id": "community-honey-badger-v2"}]} for identity in ids]})


if __name__ == "__main__":
    executable, bundle = map(Path, sys.argv[1:])
    master = ThreadingHTTPServer(("127.0.0.1", 0), Master)
    thread = threading.Thread(target=master.serve_forever, daemon=True)
    thread.start()
    try:
        result = subprocess.run([str(executable.resolve()), str(bundle.resolve()),
                                 f"http://127.0.0.1:{master.server_port}"], timeout=65, check=False)
        raise SystemExit(result.returncode)
    finally:
        master.shutdown()
        master.server_close()
        thread.join()
