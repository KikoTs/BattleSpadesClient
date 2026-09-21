"""Local-only native publication test: resumable receipts and untrusted replies."""
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading


class Handler(BaseHTTPRequestHandler):
    actions = []
    prepared = []
    committed = False
    title = ""

    def log_message(self, *args):
        pass

    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        status = 200
        if self.path == "/api/auth/login":
            value = {"account": {"legacy_id": "1000", "nickname": "FakeBuilder",
                "account_type": "registered", "identity_type": "account"}, "access_token": "test-only-token"}
        else:
            assert self.path == "/api/workshop/native"
            assert self.headers["Authorization"] == "Bearer test-only-token"
            action = body["action"]
            Handler.actions.append(action)
            if action == "prepare":
                Handler.title = body["submission"]["title"]
                if Handler.title == "Invalid Page":
                    value = {"published": True, "path": "/workshop/../../account"}
                elif Handler.committed and Handler.title == "Test Map":
                    Handler.prepared.append(body["submission"])
                    value = {"published": True, "path": "/workshop/test-map-1234"}
                else:
                    if Handler.title == "Test Map": Handler.prepared.append(body["submission"])
                    assets = []
                    for i, file in enumerate(body["submission"]["files"]):
                        content = (Handler.root / file["filename"]).read_bytes()
                        assert file["size"] == len(content)
                        assert file["sha256"] == hashlib.sha256(content).hexdigest()
                        assets.append({"id": f"00000000-0000-0000-0000-{i:012d}",
                            "filename": file["filename"], "uploadPath": "expected-path"})
                    value = {"published": False, "submission": {
                        "id": "11111111-1111-1111-1111-111111111111", "assets": assets}}
            elif action == "token":
                value = {"uploaded": True} if Handler.title == "Test Map" else {
                    "uploaded": False, "pathname": "other-path", "token": "vercel_blob_client_fake", "api_version": "12"}
            elif action == "complete":
                Handler.committed = True
                status, value = 503, {"error": "response_lost", "message": "Simulated loss after commit"}
            else:
                raise AssertionError(action)
        data = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


with tempfile.TemporaryDirectory(prefix="aos-workshop-test-") as directory:
    Handler.root = Path(directory)
    (Handler.root / "Map.ugc").write_text(json.dumps({"description": "Test", "author": "Builder", "tags": ["tdm"]}))
    (Handler.root / "Map.vxl").write_bytes(b"abc")
    (Handler.root / "Map.txt").write_text("map metadata")
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    worker = threading.Thread(target=server.serve_forever, daemon=True)
    worker.start()
    try:
        subprocess.run([sys.argv[1], "--workshop-mock", f"http://127.0.0.1:{server.server_port}", directory],
                       check=True, timeout=30)
        assert len(Handler.prepared) == 2 and Handler.prepared[0] == Handler.prepared[1]
        assert Handler.actions.count("complete") == 1
    finally:
        server.shutdown()
        server.server_close()
        worker.join()
print("Native archive HTTP recovery checks passed")
