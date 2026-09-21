"""Exercise actual libcurl collection methods against an isolated local server."""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import subprocess
import sys
import tempfile
import threading


class Handler(BaseHTTPRequestHandler):
    methods: list[str] = []
    result_attempts = 0

    def log_message(self, *args: object) -> None:
        pass

    def respond(self) -> None:
        body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))) or b"{}")
        if self.path == "/api/auth/login":
            value = {"account": {"public_id": "test", "legacy_id": "1000", "nickname": "Builder",
                     "account_type": "registered", "identity_type": "account"}, "access_token": "test-only-token"}
        elif self.path == "/api/master/stats":
            assert self.headers.get("Authorization") == "Bearer test-only-token"
            assert body == {"event_id": "retry-test", "relay_lobby_id": "test-relay"}
            Handler.result_attempts += 1
            if Handler.result_attempts == 1:
                self.send_error(503)
                return
            value = {"accepted": True}
        else:
            assert self.headers.get("Authorization") == "Bearer test-only-token"
            Handler.methods.append(self.command)
            if "equipped" in self.path:
                assert body["expected_inventory_revision"] == "2" and body["content_version"] == 5
                assert self.path.endswith("weapon%3A60%3Aview")
            if self.path.endswith("/open"):
                assert len(body["client_nonce"]) == 64
            value = {"schema_version": 1, "padding": "x" * 70000} if self.command == "GET" else {"schema_version": 1}
        encoded = json.dumps(value).encode()
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)

    do_GET = do_POST = do_PUT = do_DELETE = respond


with tempfile.TemporaryDirectory(prefix="aos-inventory-test-") as directory:
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        subprocess.run([sys.argv[1], "--http", f"http://127.0.0.1:{server.server_port}", directory],
                       check=True, timeout=30)
        assert Handler.methods == ["GET", "PUT", "DELETE", "POST"]
        assert Handler.result_attempts == 2
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
print("Native collection HTTP checks passed")
