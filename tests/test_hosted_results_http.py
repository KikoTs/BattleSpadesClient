"""Verify fair native result retries and account isolation using loopback only."""

from dataclasses import dataclass, field
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time


@dataclass
class Fixture:
    root: Path
    calls: list[tuple[str, str, int]] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)


class Handler(BaseHTTPRequestHandler):
    fixture: Fixture

    def log_message(self, *_: object) -> None:
        pass

    def do_POST(self) -> None:
        """Return permanent/transient failures while retaining later valid work."""
        try:
            body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            status = 200
            if self.path == "/api/auth/login":
                owner = body["username"] == "OwnerFixture"
                name = "owner" if owner else "other"
                value = {
                    "account": {
                        "public_id": f"hosted-{name}",
                        "legacy_id": "1000" if owner else "1001",
                        "nickname": body["username"],
                        "account_type": "registered",
                        "identity_type": "account",
                    },
                    "access_token": f"{name}-fixture-token",
                }
            else:
                assert self.path == "/api/master/stats", self.path
                event = body["event_id"]
                token = self.headers.get("Authorization", "")
                expected = "other" if event.endswith("other") else "owner"
                assert token == f"Bearer {expected}-fixture-token", (event, token)
                assert body["relay_lobby_id"] == "fixture-relay"
                self.fixture.calls.append((event, token, body["revision"]))
                (self.fixture.root / "attempt-count").write_text(
                    str(len(self.fixture.calls)), encoding="ascii"
                )
                if event == "held-other" and body["revision"] == 1:
                    (self.fixture.root / "upload-started").write_text("ready", encoding="ascii")
                    deadline = time.monotonic() + 8
                    while not (self.fixture.root / "release-upload").exists() and time.monotonic() < deadline:
                        time.sleep(0.01)
                    assert (self.fixture.root / "release-upload").exists(), "Account switch never completed"
                if event.startswith("rejected-"):
                    status = (400, 401, 503)[int(event.removeprefix("rejected-")) % 3]
                    value = {"error": "fixture_rejection", "message": "Preserve this report"}
                else:
                    value = {"accepted": True}
            encoded = json.dumps(value).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(encoded)))
            self.end_headers()
            self.wfile.write(encoded)
        except (AssertionError, KeyError, ValueError) as error:
            self.fixture.failures.append(str(error))
            self.send_error(500, "Fixture assertion failed")


def main() -> None:
    """Launch the real identity transport without contacting public services."""
    with tempfile.TemporaryDirectory(prefix="aos-hosted-results-test-") as directory:
        Handler.fixture = Fixture(Path(directory))
        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            subprocess.run(
                [sys.argv[1], "--hosted-results-mock", f"http://127.0.0.1:{server.server_port}", directory],
                check=True,
                timeout=40,
            )
            assert not Handler.fixture.failures, Handler.fixture.failures
            events = [event for event, _, _ in Handler.fixture.calls]
            assert events.count("valid-later") == 1
            assert events.count("valid-other") == 1
            assert [revision for event, _, revision in Handler.fixture.calls if event == "held-other"] == [1, 2]
        finally:
            server.shutdown()
            server.server_close()
            worker.join()
    print("Hosted result retry fairness and account-isolation HTTP checks passed")


if __name__ == "__main__":
    main()
