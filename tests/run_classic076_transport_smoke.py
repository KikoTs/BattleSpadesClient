"""Opt-in loopback 0.76 wire fixture (not a substitute for a live 0.76 server).

Exercises real ENet negotiation, cache miss, sparse player IDs, plugin challenge,
and a server-owned redirect without a second join. Requires the pyenet package.
Usage: python tests/run_classic076_transport_smoke.py path/to/aos_classic_live_smoke
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile
import time
import zlib

import enet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--port", type=int, default=32895)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535:
        parser.error("port must be between 1024 and 65535")
    executable = args.executable.resolve(strict=True)
    host = enet.Host(enet.Address(b"127.0.0.1", args.port), 8, 1, 0, 0)
    host.compress_with_range_coder()
    raw = (bytes([0, 60, 63, 0]) + bytes([96, 144, 80, 128]) * 4) * (512 * 512)
    compressed = zlib.compress(raw, 1)
    state = bytearray(Path(__file__).with_name("fixtures").joinpath("classic-tc-state-voxide.bin").read_bytes())
    peer = None
    local_id = 0
    cache_requests = joins = challenges = versions = extensions = 0
    first_spawn = None
    redirected = False
    next_update = 0.0
    connected_versions = []
    sent_updates = 0

    def send(packet, reliable=True):
        assert peer is not None
        peer.send(0, enet.Packet(packet, enet.PACKET_FLAG_RELIABLE if reliable else 0))

    def spawn(player, team=0):
        return struct.pack("<BBBBfff", 12, player, 0, team, 256.5, 256.5, 57.75) + b"Fixture\0"

    def begin_map():
        # 0.76: size, CRC, optional map name. We deliberately wait for MapCached.
        send(struct.pack("<BII", 18, len(compressed), zlib.crc32(raw)) + b"Loopback fixture\0")
        send(struct.pack("<BI", 31, 0x12345678))
        send(bytes([33]))
        send(bytes([60, 1, 192, 1]))

    with tempfile.TemporaryDirectory(prefix="battlespades-076-") as temporary:
        log_path = Path(temporary) / "client.log"
        with log_path.open("w", encoding="utf-8") as log:
            process = subprocess.Popen(
                [str(executable), "--spawn-probe", f"127.0.0.1:{args.port}"],
                stdout=log, stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW if hasattr(subprocess, "CREATE_NO_WINDOW") else 0,
            )
            try:
                deadline = time.monotonic() + 45
                while process.poll() is None:
                    now = time.monotonic()
                    if now > deadline:
                        raise RuntimeError("0.76 transport fixture timed out")
                    event = host.service(5)
                    if event.type == enet.EVENT_TYPE_CONNECT:
                        connected_versions.append(event.data)
                        if event.data != 4:
                            event.peer.disconnect(3)  # Explicit version rejection, never a timeout retry.
                        else:
                            peer = event.peer
                            begin_map()
                    elif event.type == enet.EVENT_TYPE_RECEIVE:
                        packet = event.packet.data
                        kind = packet[0]
                        if kind == 31:
                            assert packet == bytes([31, 0]), "Invalid 0.76 cache miss"
                            cache_requests += 1
                            for offset in range(0, len(compressed), 1024):
                                send(bytes([19]) + compressed[offset:offset + 1024])
                            state[1] = local_id
                            send(bytes(state))
                            send(spawn(95, 1))  # Sparse 0.76 WorldUpdate row beyond the 0.75 limit.
                            if redirected:
                                send(spawn(local_id))
                        elif kind == 32:
                            assert packet == struct.pack("<BI", 32, 0x12345678)
                            challenges += 1
                        elif kind == 34:
                            assert packet[1:5] == b"b\x00\x01\x00" and packet.endswith(b"\0")
                            versions += 1
                        elif kind == 60:
                            assert packet == bytes([60, 0]), "Client claimed unsupported extensions"
                            extensions += 1
                        elif kind == 9:
                            assert not redirected and packet[1] == local_id, "Stale or duplicate join"
                            assert len(packet) >= 13 and packet[-1] == 0
                            joins += 1
                            send(spawn(local_id))
                            first_spawn = now
                        elif kind == 30:
                            assert packet == bytes([30, local_id, 0]), "Invalid weapon selection"
                        else:
                            raise AssertionError(f"Unexpected outgoing packet {kind}: {packet.hex()}")
                    if first_spawn is not None and now >= next_update:
                        row = struct.pack("<Bffffff", 95, 262.5, 256.5, 57.75, -1, 0, 0)
                        send(bytes([2]) + row, reliable=False)
                        sent_updates += 1
                        next_update = now + 0.05
                    if first_spawn is not None and not redirected and now - first_spawn >= 2:
                        redirected = True
                        local_id = 65
                        begin_map()
                    host.flush()
                assert process.returncode == 0, "Client rejected the 0.76 fixture"
                assert connected_versions == [168, 3, 4], connected_versions
                assert joins == 1 and cache_requests == 2
                assert challenges == versions == extensions == 2
                assert sent_updates >= 200
                print(log_path.read_text(encoding="utf-8"))
                print("0.76 ENet fallback, cache misses, sparse IDs, plugin replies and redirect passed; one join only.")
            except Exception:
                print(log_path.read_text(encoding="utf-8"))
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)


if __name__ == "__main__":
    main()
