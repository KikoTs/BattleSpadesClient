"""Regression tests for resumable WinHTTP downloads, using only localhost."""

from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import subprocess
import sys
import tempfile
import threading


PAYLOAD = b"verified synthetic package contents" * 100
PREFIX = 317


class Handler(BaseHTTPRequestHandler):
    """Serve correct and intentionally broken byte ranges."""

    requests: list[tuple[str, str | None]] = []

    def log_message(self, *_args: object) -> None:
        """Suppress routine request logs."""

    def do_GET(self) -> None:
        """Answer one test download."""
        header = self.headers.get("Range")
        self.requests.append((self.path, header))
        start = int(header[6:-1]) if header else 0
        body = PAYLOAD[start:]
        status = 206 if start else 200
        if self.path == "/ignore":
            status, start, body = 200, 0, PAYLOAD
        if self.path == "/unsatisfied" and start:
            self.send_error(416)
            return
        if self.path == "/oversize":
            body = PAYLOAD + b"x"
        self.send_response(status)
        if status == 206 and self.path != "/missing":
            first = 0 if self.path == "/wrong-start" else start
            total = len(PAYLOAD) + 1 if self.path == "/wrong-total" else len(PAYLOAD)
            self.send_header("Content-Range", f"bytes {first}-{len(PAYLOAD) - 1}/{total}")
        if self.path != "/oversize-no-length":
            self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if self.path == "/drop":
            body = body[:101]
        if self.path == "/oversize-no-length":
            body = PAYLOAD + b"x"
        self.wfile.write(body)
        self.close_connection = True


def main() -> None:
    """Keep valid prefixes, reject bad ranges, and bound bytes written."""
    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        base = f"http://127.0.0.1:{server.server_port}"
        try:
            with tempfile.TemporaryDirectory() as scratch:
                target = Path(scratch) / "Игры-package.partial"

                def download(route: str) -> subprocess.CompletedProcess[str]:
                    return subprocess.run(
                        [sys.argv[1], base + route, str(target), str(len(PAYLOAD))],
                        capture_output=True, text=True, timeout=15,
                    )

                for route in ("/valid", "/ignore", "/unsatisfied"):
                    target.write_bytes(PAYLOAD[:PREFIX])
                    result = download(route)
                    assert result.returncode == 0, (route, result.stderr)
                    assert target.read_bytes() == PAYLOAD, route
                for route in ("/wrong-start", "/wrong-total", "/missing"):
                    target.write_bytes(PAYLOAD[:PREFIX])
                    result = download(route)
                    assert result.returncode == 1 and "Content-Range" in result.stderr, result.stderr
                    assert target.read_bytes() == PAYLOAD[:PREFIX], route
                    result = download("/valid")
                    assert result.returncode == 0 and target.read_bytes() == PAYLOAD
                for route in ("/oversize", "/oversize-no-length"):
                    target.write_bytes(b"")
                    result = download(route)
                    assert result.returncode == 1, (route, result.stderr)
                    assert target.stat().st_size <= len(PAYLOAD), route
                target.write_bytes(PAYLOAD[:PREFIX])
                result = download("/drop")
                assert result.returncode == 1
                assert target.read_bytes() == PAYLOAD[:PREFIX + 101]
                result = download("/valid")
                assert result.returncode == 0 and target.read_bytes() == PAYLOAD
        finally:
            server.shutdown()
            thread.join(timeout=5)
    print("aos_updater_http_tests: ok")


if __name__ == "__main__":
    main()
