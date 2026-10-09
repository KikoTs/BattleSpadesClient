"""Isolated auth fixture: no Valve/master credentials or production accounts."""
import json
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Handler(BaseHTTPRequestHandler):
    recovery_version = "version-one"
    def log_message(self, *_):
        pass

    def do_POST(self):
        value = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        linking = self.path.endswith("/link")
        logging_in = self.path.endswith("/login")
        expected = ({"client", "username", "password"} if logging_in else
                    {"client", "app_id", "ticket"} if self.path.endswith("/ticket") or linking else
                    {"client", "steam_id", "recovery_code"})
        authorization = self.headers.get("Authorization")
        if set(value) != expected or (authorization != "Bearer local-fixture-bearer" if linking else bool(authorization)):
            self.send_error(400)
            return
        steam_id = "76561198000000002" if value.get("ticket") == "cc" else "76561198000000001"
        response = {
            "account": {"public_id": "aos-test-account", "legacy_id": "10001", "steam_id": steam_id,
                        "nickname": "PermanentName", "registered_name": "PermanentName", "display_name": "Steam Persona",
                        "identity_type": "steam", "account_type": "registered", "ranked_eligible": True},
            "access_token": "local-fixture-bearer", "session": {"expires_at": "2099-01-01T00:00:00Z"},
        }
        if logging_in or linking:
            response["account"]["public_id"] = "existing-account" if value.get("ticket") != "ee" else "wrong-account"
            response["account"]["registered_name"] = "ExistingPlayer"
        if logging_in:
            response["account"]["steam_id"] = ""
            response["account"]["identity_type"] = "password"
        if value.get("ticket") == "aa":
            response["recovery_code"] = "AOS-TEST-FIRST-CODE-ONLY"
        elif self.path.endswith("/recover"):
            response["recovery_code"] = "AOS-TEST-SECOND-CODE-ONLY"
            Handler.recovery_version = "version-two"
        elif value.get("ticket") == "dd":
            Handler.recovery_version = "rotated-on-another-device"
        response["recovery_code_version"] = Handler.recovery_version
        data = json.dumps(response).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server, tempfile.TemporaryDirectory(prefix="bs-steam-auth-") as root:
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        completed = subprocess.run([sys.argv[1], "--steam-identity-mock", f"http://127.0.0.1:{server.server_port}", root], timeout=30)
    finally:
        server.shutdown()
    sys.exit(completed.returncode)
