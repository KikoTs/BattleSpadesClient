#include "battlespades/frontend/gameplay_debug_presentation.hpp"
#include "battlespades/world/gameplay_debug_lab.hpp"

#include <iostream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}
}

int main() {
    using namespace battlespades;
    try {
        world::GameplayDebugLab lab;
        expect(lab.selected_class_id() == 0U && lab.selected_tool_id() == 0U,
               "lab must open deterministically on Soldier/tool 0");
        lab.cycle_class(-1);
        expect(lab.selected_class_id() == 17U, "class browser must wrap backward");
        lab.cycle_tool(-1);
        expect(lab.selected_tool_id() == 64U, "tool browser must wrap across all 65 tools");
        lab.cycle_tool(1);
        expect(lab.selected_tool_id() == 0U, "tool browser must wrap forward");
        expect(lab.select_class_and_tool(4U, 24U),
               "lab must expose a deterministic Zombie hand parity preset");
        expect(lab.selected_class_id() == 4U && lab.selected_tool_id() == 24U,
               "Zombie hand preset must select the retail class/tool pair");
        expect(!lab.select_class_and_tool(255U, 255U),
               "invalid direct parity selection must fail closed");
        expect(lab.select_class_and_tool(0U, 0U),
               "lab must return to an ordinary parity pair after a preset");

        lab.spawn_all_classes();
        expect(lab.spawned_classes().size() == 18U,
               "gallery command must request every class model");
        expect(lab.inspection_mode() == world::DebugInspectionMode::gallery,
               "gallery command must enter the inspectable gallery view");
        lab.clear_spawned();
        expect(lab.spawned_classes().empty(), "clear must remove every mannequin");
        lab.spawn_selected_class();
        expect(lab.spawned_classes().size() == 1U,
               "selected-class spawn must be idempotent and bounded");
        expect(lab.inspection_mode() == world::DebugInspectionMode::character,
               "selected spawn must enter character-and-held-tool view");

        lab.toggle_team();
        expect(lab.team_index() == 1U, "team preview must switch to green");
        expect(lab.team_color() == world::VxlColor{137U, 179U, 44U, 255U},
               "green must use the retail server default RGB");
        lab.cycle_color_channel();
        lab.adjust_team_color(5);
        expect(lab.team_index() == 2U && lab.team_color().green == 184U,
               "debug lab must inspect arbitrary server-defined team RGB");
        lab.cycle_inspection_mode();
        expect(lab.inspection_mode() == world::DebugInspectionMode::first_person,
               "TAB view cycle must expose the first-person weapon pose");
        lab.cycle_inspection_mode();
        expect(lab.inspection_mode() == world::DebugInspectionMode::gallery,
               "TAB view cycle must expose the class gallery");
        lab.cycle_inspection_mode();
        expect(lab.inspection_mode() == world::DebugInspectionMode::effects &&
                   lab.vfx_particles().live_count() == 40U,
               "offline lab must expose retail's exact forty-particle death preview");
        lab.cycle_vfx(1);
        expect(lab.vfx_kind() == world::DebugVfxKind::grave &&
                   lab.vfx_particles().live_count() == 28U,
               "VFX lab must replay tombstone display destruction independently");
        lab.cycle_vfx(1);
        // Grenade.update (gameScene 0x100AE790) creates 8 glow blocks; only
        // the GLGrenade (ExplodeOnImpactEntity) uses 4.
        expect(lab.vfx_particles().live_count() == 18U,
               "VFX lab grenade replay must use the recovered 8+10 burst");
        lab.cycle_vfx(1);
        expect(lab.vfx_particles().live_count() == 18U,
               "VFX lab rocket replay must use the recovered 8+10 burst");
        lab.cycle_inspection_mode();
        expect(lab.inspection_mode() == world::DebugInspectionMode::character,
               "VFX mode must wrap back to character parity");
        lab.cycle_skin(1);
        expect(lab.skin_id() == "mafia", "lab must expose the shipped mafia skin");
        lab.cycle_skin(1);
        expect(lab.skin_id() == "default", "skin browser must wrap");

        lab.cycle_tool(17); // direction is a sign: advances one to tool 1.
        expect(lab.selected_tool_id() == 1U, "positive tool cycle must advance once");
        lab.set_primary(true);
        lab.tick(1.0 / 60.0);
        lab.set_primary(false);
        lab.tick(1.0 / 60.0);
        expect(lab.emitted_action_count() > 0U,
               "debug fire must exercise the real weapon runtime");

        frontend::GameplayDebugPresentation presentation;
        const auto draw = presentation.build(lab, {1280, 720});
        expect(!draw.empty(), "debug lab must produce its in-world control/status HUD");
        std::cout << "gameplay debug lab: class, skin, weapon and model controls passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "gameplay debug lab failure: " << error.what() << '\n';
        return 1;
    }
}
