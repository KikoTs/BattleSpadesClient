"""Optional local interoperability fixture; does not relax server validation."""
import json
from pathlib import Path
from twisted.internet import reactor
from pyspades.contained import WeaponReload


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
            if self.name == "BS base refill":
                # Low-supply fixture; ordinary position/contact validation
                # and the server's normal base refill path remain in charge.
                self.blocks = 7
                self.grenades = 2
                self.set_hp(37)
                self.weapon_object.current_ammo = 4
                self.weapon_object.current_stock = 11
                ammo = WeaponReload()
                ammo.player_id = self.player_id
                ammo.clip_ammo, ammo.reserve_ammo = 4, 11
                self.send_contained(ammo)
                self.team.base.set(pos[0] + 6, pos[1], 60)
                self.team.base.update()
            if self.name == "BS rotation" and not getattr(self.protocol, "rotation_test", False):
                self.protocol.rotation_test = True
                reactor.callLater(2, self.protocol.advance_rotation)
            return super().on_spawn(pos)

        def refill(self, local=False):
            result = super().refill(local)
            if self.name == "BS base refill" and not local:
                emit("base_refill", self, blocks=self.blocks, grenades=self.grenades, hp=self.hp,
                     ammo=self.weapon_object.current_ammo, reserve=self.weapon_object.current_stock)
            return result

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
            exercise_refill = Path(__file__).with_name("infiblocks.py").exists()
            if exercise_refill:
                # Establish a low-supply fixture after normal line validation;
                # the unmodified infiblocks script owns every refill packet.
                self.blocks = 25
                self.grenades = 1
                self.hp = 64
                self.weapon_object.current_ammo = 4
                self.weapon_object.current_stock = 11
            result = super().on_line_build(points)
            if exercise_refill:
                emit("refill", self, blocks=self.blocks, grenades=self.grenades, hp=self.hp,
                     ammo=self.weapon_object.current_ammo, reserve=self.weapon_object.current_stock)
            return result

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
