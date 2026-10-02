"""BattleSpadesAssetInstaller --download against a local HTTP server.

Exercises the real libcurl transport: stable.json -> retail_assets -> a
synthetic ZIP (a few text files, never retail content) served with Range
support, the first transfer dropped half-way so the installer must resume.
Also checks that a manifest without retail_assets explains that the download
is not available instead of mentioning a button that does not exist.
"""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import zipfile

ROOT = "BattleSpades-retail-assets-1.0.0"
FILES = {
    "ambients/amb_test.ogg": b"ambient bytes",
    "png/ui/logo.png": b"logo bytes",
    "maps/Training.vxl": b"map bytes" * 1000,
}


def make_pack():
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, data in FILES.items():
            archive.writestr(f"{ROOT}/{name}", data)
        archive.writestr(f"{ROOT}/list.pnq", b"root file")
    return buffer.getvalue()


PACK = make_pack()


class Handler(BaseHTTPRequestHandler):
    manifests = {}
    requests = []
    dropped = False

    def log_message(self, *args):
        pass

    def do_GET(self):
        Handler.requests.append((self.path, self.headers.get("Range")))
        if self.path in Handler.manifests:
            body = json.dumps(Handler.manifests[self.path]).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path != "/files/pack.zip":
            self.send_error(404)
            return
        start = 0
        header = self.headers.get("Range")
        if header:
            assert header.startswith("bytes=") and header.endswith("-"), header
            start = int(header[len("bytes="):-1])
        body = PACK[start:]
        self.send_response(206 if start else 200)
        if start:
            self.send_header("Content-Range", f"bytes {start}-{len(PACK) - 1}/{len(PACK)}")
        self.send_header("Content-Type", "application/zip")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        if not Handler.dropped:
            # First transfer: send half, then drop the connection.
            Handler.dropped = True
            self.wfile.write(body[: len(body) // 2])
            self.wfile.flush()
            self.close_connection = True
            self.connection.shutdown(2)
            return
        self.wfile.write(body)


def manifest(base, with_retail):
    components = {
        "client": {
            "version": "0.2.1",
            "package": "client.zip",
            "urls": [f"{base}/files/client.zip"],
            "size": 10,
            "sha256": "0" * 64,
        }
    }
    if with_retail:
        components["retail_assets"] = {
            "version": "1.0.0",
            "package": f"{ROOT}.zip",
            "urls": [f"{base}/files/missing.zip", f"{base}/files/pack.zip"],
            "size": len(PACK),
            "sha256": hashlib.sha256(PACK).hexdigest(),
            "root": ROOT,
            "target": "assets/original",
        }
    return {"schema": 2, "product": "BattleSpades", "channel": "test", "components": components}


def catalog(path):
    entries = [{"path": name, "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}
               for name, data in FILES.items()]
    path.write_text(json.dumps({
        "schema": 1,
        "file_count": len(entries),
        "total_bytes": sum(len(data) for data in FILES.values()),
        "files": entries,
    }), encoding="utf-8")


def run(installer, *args):
    return subprocess.run([installer, *args], capture_output=True, text=True, timeout=90)


def main():
    installer = sys.argv[1]
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{server.server_address[1]}"
    Handler.manifests = {"/stable.json": manifest(base, True), "/empty.json": manifest(base, False)}
    try:
        with tempfile.TemporaryDirectory() as scratch:
            work = Path(scratch) / "Игры BattleSpades"
            work.mkdir()
            catalog_file = work / "asset-manifest.json"
            catalog(catalog_file)

            # 1. Download, resume, verify, extract, import.
            destination = work / "install" / "assets" / "original"
            report = work / "report.txt"
            result = run(installer, "--download", "--manifest-url", f"{base}/stable.json",
                         "--manifest", str(catalog_file), "--destination", str(destination),
                         "--report", str(report))
            assert result.returncode == 0, (result.returncode, result.stdout, result.stderr,
                                            report.read_text(encoding="utf-8") if report.exists() else "")
            assert report.read_text(encoding="utf-8") == "ok"
            for name, data in FILES.items():
                assert (destination / name).read_bytes() == data, name
            ranges = [value for path, value in Handler.requests if path == "/files/pack.zip"]
            assert ranges and ranges[0] is None and any(value for value in ranges[1:]), ranges
            assert not (destination.parent / ".retail-download").exists(), "cache removed after success"

            # 2. No retail_assets: a clear message, no phantom button.
            report2 = work / "report2.txt"
            result = run(installer, "--download", "--manifest-url", f"{base}/empty.json",
                         "--manifest", str(catalog_file), "--destination", str(work / "other"),
                         "--report", str(report2))
            text = report2.read_text(encoding="utf-8")
            assert result.returncode == 1, (result.returncode, text)
            assert "isn't available yet" in text and "https://www.aosplay.net/download" in text, text
            assert "Download game assets" not in text, text

            # 3. Server unreachable: an offline message.
            report3 = work / "report3.txt"
            result = run(installer, "--download", "--manifest-url", "http://127.0.0.1:9/stable.json",
                         "--manifest", str(catalog_file), "--destination", str(work / "other"),
                         "--report", str(report3))
            text = report3.read_text(encoding="utf-8")
            assert result.returncode == 1 and "could not be reached" in text, text

            # 4. A folder import that finds nothing never mentions the download.
            empty = work / "steamapps" / "common"
            empty.mkdir(parents=True)
            report4 = work / "report4.txt"
            result = run(installer, "--source", str(empty), "--manifest", str(catalog_file),
                         "--destination", str(work / "other"), "--report", str(report4))
            text = report4.read_text(encoding="utf-8")
            assert result.returncode == 1 and "no Ace of Spades game files were found" in text, text
            assert "Download game assets" not in text, text
    finally:
        server.shutdown()
    print("aos_retail_download_http_tests: ok")


if __name__ == "__main__":
    main()
