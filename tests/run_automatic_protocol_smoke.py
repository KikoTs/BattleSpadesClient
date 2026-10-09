"""Opt-in ENet tests for the automatic protocol's bounded initial listen window.

Uses only loopback. Requires pyenet and the aos_classic_live_smoke executable.
"""

import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib

import enet


def exercise(executable, port, scenario, compressed, state):
    host = enet.Host(enet.Address(b"127.0.0.1", port), 8, 1, 0, 0)
    host.compress_with_range_coder()
    endpoint = f"127.0.0.1:{port}"
    if scenario == "pinned-retail":
        endpoint = "aosbb://" + endpoint
    versions = []
    ticket_elapsed = None
    pending_map = None
    connected_at = None
    peer = None
    with tempfile.TemporaryDirectory(prefix="battlespades-auto-") as temporary:
        log_path = Path(temporary) / "client.log"
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(
                [str(executable), "--probe", endpoint], stdout=log, stderr=log,
                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
            )
            try:
                deadline = time.monotonic() + 8
                while process.poll() is None:
                    now = time.monotonic()
                    if now > deadline:
                        raise AssertionError(f"{scenario}: fixture timed out")
                    event = host.service(5)
                    if event.type == enet.EVENT_TYPE_CONNECT:
                        versions.append(event.data)
                        peer = event.peer
                        connected_at = time.monotonic()
                        if scenario == "legacy-map-start":
                            if event.data == 168:
                                # Let the old eager-ticket code fail before the
                                # server sends its recognizable legacy header.
                                pending_map = connected_at + 0.08
                            else:
                                assert event.data == 3, versions
                                peer.send(0, enet.Packet(struct.pack("<BI", 18, len(compressed)), enet.PACKET_FLAG_RELIABLE))
                                for offset in range(0, len(compressed), 1024):
                                    peer.send(0, enet.Packet(bytes([19]) + compressed[offset:offset + 1024], enet.PACKET_FLAG_RELIABLE))
                                peer.send(0, enet.Packet(state, enet.PACKET_FLAG_RELIABLE))
                        else:
                            assert event.data == 168, versions
                    elif event.type == enet.EVENT_TYPE_RECEIVE:
                        packet = event.packet.data
                        assert scenario != "legacy-map-start", (
                            f"A retail/gameplay packet leaked into legacy probing: {packet.hex()}"
                        )
                        assert ticket_elapsed is None, "Duplicate initial ticket"
                        assert packet.startswith(bytes([0x30, 105])), "Expected retail ticket envelope"
                        ticket_elapsed = time.monotonic() - connected_at
                        if scenario == "silent-retail":
                            assert 0.15 <= ticket_elapsed < 2.0, ticket_elapsed
                        else:
                            assert ticket_elapsed < 0.2, ticket_elapsed
                        event.peer.disconnect(10)  # Expected fixture end; no map is served.
                    if pending_map is not None and time.monotonic() >= pending_map:
                        peer.send(0, enet.Packet(struct.pack("<BI", 18, len(compressed)), enet.PACKET_FLAG_RELIABLE))
                        pending_map = None
                    host.flush()
                if scenario == "legacy-map-start":
                    assert versions == [168, 3], versions
                    assert process.returncode == 0, log_path.read_text(encoding="utf-8")
                else:
                    assert versions == [168] and ticket_elapsed is not None
                    assert process.returncode == 2, process.returncode
                print(f"{scenario}: versions={versions}, ticket_ms={None if ticket_elapsed is None else round(ticket_elapsed * 1000, 1)} PASS")
            except Exception:
                print(log_path.read_text(encoding="utf-8"))
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--port", type=int, default=32884)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65533:
        parser.error("port must leave room for three consecutive loopback ports")
    executable = args.executable.resolve(strict=True)
    raw = (bytes([0, 60, 63, 0]) + bytes([96, 144, 80, 128]) * 4) * (512 * 512)
    compressed = zlib.compress(raw, 1)
    state = bytearray(Path(__file__).with_name("fixtures").joinpath("classic-tc-state-voxide.bin").read_bytes())
    state[1] = 0
    for offset, scenario in enumerate(("legacy-map-start", "silent-retail", "pinned-retail")):
        exercise(executable, args.port + offset, scenario, compressed, bytes(state))


if __name__ == "__main__":
    main()
