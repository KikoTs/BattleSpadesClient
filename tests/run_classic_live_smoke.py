"""Opt-in loopback-only piqueserver test. Requires piqueserver in this Python env.

Usage: python tests/run_classic_live_smoke.py path/to/aos_classic_live_smoke[.exe]
Creates a fresh map/config in a temporary directory and stops all its processes.
"""
import argparse
import concurrent.futures
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import enet


def target_peer(port, events_path):
    # Wait for the actual first spawn, not a fixed delay: version fallback
    # can take longer on a busy desktop, leaving slot 0 free in the meantime.
    deadline = time.monotonic() + 20
    while True:
        if events_path.exists():
            events = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines() if line]
            if any(event["event"] == "spawn" and event["player"] == 0 for event in events):
                break
        if time.monotonic() > deadline:
            raise RuntimeError("The shooter did not spawn before the target joined")
        time.sleep(0.05)
    host = enet.Host(None, 1, 1, 0, 0)
    host.compress_with_range_coder()
    peer = host.connect(enet.Address(b"127.0.0.1", port), 1, 3)

    def send(data):
        peer.send(0, enet.Packet(data, enet.PACKET_FLAG_RELIABLE))
        host.flush()

    deadline = time.monotonic() + 18
    while time.monotonic() < deadline:
        event = host.service(10)
        if event.type == enet.EVENT_TYPE_RECEIVE:
            packet = event.packet.data
            if packet[0] == 31:
                send(b"\x20" + packet[1:])
            elif packet[0] == 33:
                send(b"\x22b\x00\x01\x00LocalTest\0")
            elif packet[0] == 60:
                send(b"\x3c\x00")
            elif packet[0] == 15:
                if packet[1] != 1:
                    raise RuntimeError("The test target did not receive slot 1")
                send(struct.pack("<BBBBBiBBB", 9, 1, 1, 0, 2, 0, 112, 112, 112) + b"Local target\0")
        elif event.type == enet.EVENT_TYPE_DISCONNECT:
            raise RuntimeError(f"Local target disconnected: {event.data}")
    peer.disconnect(0)
    host.flush()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--port", type=int, default=32889)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    if not 1024 <= args.port <= 65535:
        parser.error("port must be between 1024 and 65535")
    with tempfile.TemporaryDirectory(prefix="battlespades-classic-") as temporary:
        root = Path(temporary)
        (root / "maps").mkdir()
        (root / "scripts").mkdir()
        column = bytes([0, 60, 63, 0]) + bytes([0, 255, 0, 128]) * 4
        (root / "maps/flat.vxl").write_bytes(column * (512 * 512))
        (root / "maps/flat.txt").write_text("name = 'Flat local test'\n", encoding="utf-8")
        shutil.copyfile(Path(__file__).with_name("piqueserver_observe.py"), root / "scripts/observe.py")
        (root / "config.toml").write_text(
            f'name = "Classic local test"\nmaster = false\nnetwork_interface = "127.0.0.1"\n'
            f'port = {args.port}\nip_getter = ""\nmax_connections_per_ip = 8\n'
            'scripts = ["observe"]\nrotation = ["flat"]\ngame_mode = "ctf"\n'
            'respawn_time = "2sec"\nrespawn_waves = false\n', encoding="utf-8"
        )
        with (root / "server.log").open("w", encoding="utf-8") as log:
            server = subprocess.Popen(
                [sys.executable, "-m", "piqueserver", "-d", str(root)], stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if sys.platform == "win32" else 0,
            )
            try:
                deadline = time.monotonic() + 15
                while "Map loaded successfully" not in (root / "server.log").read_text(encoding="utf-8", errors="replace"):
                    if server.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Local piqueserver did not start")
                    time.sleep(0.1)
                with concurrent.futures.ThreadPoolExecutor(max_workers=1) as pool:
                    target = pool.submit(target_peer, args.port, root / "events.jsonl")
                    subprocess.run([str(executable), str(args.port), "--exercise"], check=True, timeout=45)
                    target.result(timeout=25)
                subprocess.run([str(executable), str(args.port), "--rotation"], check=True, timeout=45)
                events = [json.loads(line) for line in (root / "events.jsonl").read_text(encoding="utf-8").splitlines()]
                kinds = {event["event"] for event in events}
                required = {"hit", "kill", "build", "line", "remove", "grenade", "color", "jump"}
                if "hack" in kinds or not required <= kinds:
                    raise RuntimeError(f"Missing accepted actions or hack report: {events}")
                if not any(event["event"] == "build" and event["color"] == [224,80,32] for event in events):
                    raise RuntimeError("Server did not build with the selected palette color")
                if sum(event["event"] == "jump" and event["player"] == 0 for event in events) != 1:
                    raise RuntimeError("Held SPACE must result in exactly one server jump")
                print("Accepted hit, kill, colored build, block line, dig, grenade and reload; no corrections or hack reports.")
            except Exception:
                print((root / "server.log").read_text(encoding="utf-8", errors="replace"), file=sys.stderr)
                raise
            finally:
                # A Windows venv python.exe launches a child interpreter. Stop
                # this test's entire process tree so that child's logs close.
                if sys.platform == "win32" and server.poll() is None:
                    subprocess.run(["taskkill", "/PID", str(server.pid), "/T", "/F"],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
                elif server.poll() is None:
                    server.terminate()
                try:
                    server.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()


if __name__ == "__main__":
    main()
