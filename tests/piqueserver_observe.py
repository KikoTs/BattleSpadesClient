"""Optional local interoperability fixture; does not relax server validation."""
import json
from pathlib import Path
from twisted.internet import reactor


def emit(kind, player, **values):
    path = Path(__file__).resolve().parent.parent / "events.jsonl"
    with path.open("a", encoding="utf-8") as stream:
        stream.write(json.dumps({"event": kind, "player": player.player_id, **values}) + "\n")


def apply_script(protocol, connection, config):
    class Observed(connection):
        def on_spawn_location(self, pos):
            return (250.5 + self.player_id * 5, 250.5, 57.75)

        def on_spawn(self, pos):
            emit("spawn", self)
            if self.name == "BS rotation" and not getattr(self.protocol, "rotation_test", False):
                self.protocol.rotation_test = True
                reactor.callLater(2, self.protocol.advance_rotation)
            return super().on_spawn(pos)

        def on_hack_attempt(self, reason):
            emit("hack", self, reason=reason)
            return super().on_hack_attempt(reason)

        def on_hit(self, amount, target, kill_type, grenade):
            emit("hit", self, target=target.player_id, amount=amount)
            return super().on_hit(amount, target, kill_type, grenade)

        def on_kill(self, by, kill_type, grenade):
            emit("kill", self)
            return super().on_kill(by, kill_type, grenade)

        def on_block_build(self, x, y, z):
            emit("build", self, cell=[x, y, z], color=self.protocol.map.get_color(x, y, z))
            return super().on_block_build(x, y, z)

        def on_color_set(self, color):
            emit("color", self, color=color)
            return super().on_color_set(color)

        def on_line_build(self, points):
            emit("line", self, cells=list(points))
            return super().on_line_build(points)

        def on_animation_update(self, jump, crouch, sneak, sprint):
            if jump:
                emit("jump", self)
            return super().on_animation_update(jump, crouch, sneak, sprint)

        def on_block_removed(self, x, y, z):
            emit("remove", self, cell=[x, y, z])
            return super().on_block_removed(x, y, z)

        def on_grenade_thrown(self, grenade):
            emit("grenade", self)
            return super().on_grenade_thrown(grenade)

    return protocol, Observed
