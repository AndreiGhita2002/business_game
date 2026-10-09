//
// Created by Andrei Ghita on 04.10.2026.
//

#include "VehiclePanel.hpp"

#include <raymath.h>
#include <rlgl.h>

#include "UIButton.hpp"
#include "UIView.hpp"
#include "entity/EntityManager.hpp"
#include "entity/SimConvert.hpp"
#include "game/Picking.hpp"
#include "voxel/VoxelView.hpp"

// How much one press of Faster or Slower changes the speed: a twentieth of a
// voxel per tick, which is one voxel per second at 20 ticks a second
static const sim::Fixed SPEED_STEP = sim::Fixed::from_ratio(1, 20);

#define VEHICLE_HIGHLIGHT_COLOUR YELLOW

std::string& VehiclePanel::get_view_type() {
    static std::string TYPE = VEHICLE_PANEL_STR;
    return TYPE;
}

VehiclePanel::VehiclePanel(ViewNode* parent, VoxelView* voxel_view, EntityManager* entities,
                           const sim::Simulation* sim, sim::CommandQueue* commands)
    : UINode(parent, Rectangle{0.0f, 84.0f, 0.0f, 0.0f}, Anchor::TOP_CENTER),
      voxel_view(voxel_view), entities(entities), sim(sim), commands(commands),
      has_selection(false), selected{}
{
    // One row of buttons along the bottom, cut evenly from the panel's width
    const float button_width = (VEHICLE_PANEL_WIDTH - (VEHICLE_PANEL_BUTTON_COUNT + 1) * VEHICLE_PANEL_GAP)
                               / VEHICLE_PANEL_BUTTON_COUNT;
    const float button_y = VEHICLE_PANEL_GAP
                         + VEHICLE_PANEL_LINE_COUNT * VEHICLE_PANEL_LINE_HEIGHT
                         + VEHICLE_PANEL_GAP;

    struct ButtonDef { const char* text; std::function<void()> action; };
    const ButtonDef buttons[VEHICLE_PANEL_BUTTON_COUNT] = {
        {"Slower", [this] { slower(); }},
        {"Faster", [this] { faster(); }},
        {"Reverse", [this] { reverse(); }},
        {"Remove", [this] { remove(); }},
    };
    for (int i = 0; i < VEHICLE_PANEL_BUTTON_COUNT; ++i) {
        add_child(std::make_unique<UIButton>(this, buttons[i].text, buttons[i].action,
            Rectangle{
                VEHICLE_PANEL_GAP + static_cast<float>(i) * (button_width + VEHICLE_PANEL_GAP),
                button_y, button_width, VEHICLE_PANEL_BUTTON_HEIGHT
            }, Anchor::TOP_LEFT));
    }
}

Vector2 VehiclePanel::measure() {
    return Vector2{
        VEHICLE_PANEL_WIDTH,
        VEHICLE_PANEL_GAP + VEHICLE_PANEL_LINE_COUNT * VEHICLE_PANEL_LINE_HEIGHT
            + VEHICLE_PANEL_GAP + VEHICLE_PANEL_BUTTON_HEIGHT + VEHICLE_PANEL_GAP
    };
}

void VehiclePanel::select(const sim::VehicleId id) {
    has_selection = true;
    selected = id;
    refresh_lines();
}

void VehiclePanel::clear_selection() {
    has_selection = false;
    selected = {};
}

void VehiclePanel::update(const float delta_time) {
    if (!isEnabled) return;

    // Removed, by this panel's own button or otherwise
    if (has_selection && !sim->vehicles().contains(selected)) clear_selection();

    pick_from_world();
    if (has_selection && IsKeyPressed(KEY_ESCAPE)) clear_selection();
    if (has_selection) refresh_lines();

    ViewNode::update(delta_time);
}

void VehiclePanel::pick_from_world() {
    if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) return;

    // A click on any UI is not a click on the world, and neither is one that
    // another tool is waiting for. UIView has routed the mouse by now.
    const UIView* ui = get_view();
    if (ui != nullptr && ui->mouse_consumed) return;
    if (world_click_taken && world_click_taken()) return;

    const Ray ray = GetScreenToWorldRay(GetMousePosition(), voxel_view->camera);
    VoxelRayHit hit{};
    const VehicleEntity* entity = nullptr;
    if (find_voxel_on_ray(ray, &voxel_view->voxel_grids, nullptr, &hit)) {
        entity = entities->vehicle_for_grid(hit.grid);
    }

    // Anything that is not a vehicle, the terrain included, deselects
    if (entity != nullptr) select(entity->vehicle_id());
    else clear_selection();
}

void VehiclePanel::refresh_lines() {
    const sim::Vehicle* vehicle = sim->vehicles().get(selected);
    if (vehicle == nullptr) return;

    const sim::Route* route = sim->routes().get(vehicle->route);
    const float per_second = to_float(vehicle->speed) / tick_seconds();

    lines[0] = TextFormat("vehicle %u (%s)%s", selected.index, vehicle->model.c_str(),
                          entities->vehicle(selected) ? "" : ", not drawn");
    lines[1] = TextFormat("speed %.2f voxels/s", per_second);
    lines[2] = route
        ? TextFormat("at %.1f of %.0f along its route", to_float(vehicle->distance), to_float(route->length()))
        : "on no route";
}

void VehiclePanel::faster() const {
    const sim::Vehicle* vehicle = sim->vehicles().get(selected);
    if (!has_selection || vehicle == nullptr) return;
    // Faster in whichever direction it is already going
    const sim::Fixed step = vehicle->speed < sim::Fixed{} ? -SPEED_STEP : SPEED_STEP;
    commands->submit(std::make_unique<sim::SetVehicleSpeed>(selected, vehicle->speed + step));
}

void VehiclePanel::slower() const {
    const sim::Vehicle* vehicle = sim->vehicles().get(selected);
    if (!has_selection || vehicle == nullptr) return;
    // Towards zero, and no further: slowing down never turns a vehicle round
    sim::Fixed speed = vehicle->speed;
    if (speed > SPEED_STEP) speed -= SPEED_STEP;
    else if (speed < -SPEED_STEP) speed += SPEED_STEP;
    else speed = sim::Fixed{};
    commands->submit(std::make_unique<sim::SetVehicleSpeed>(selected, speed));
}

void VehiclePanel::reverse() const {
    const sim::Vehicle* vehicle = sim->vehicles().get(selected);
    if (!has_selection || vehicle == nullptr) return;
    commands->submit(std::make_unique<sim::SetVehicleSpeed>(selected, -vehicle->speed));
}

void VehiclePanel::remove() const {
    if (!has_selection) return;
    // The selection clears itself once the simulation has removed the vehicle
    commands->submit(std::make_unique<sim::DespawnVehicle>(selected));
}

void VehiclePanel::render() {
    if (!isEnabled) return;

    // Only the panel and its buttons are held back while nothing is selected.
    // The sibling chain is still walked, the same as ShaderMenu does.
    if (has_selection) {
        draw_selection_highlight();
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* VehiclePanel::hit_test(const Vector2 point) {
    // A hidden panel must not swallow world clicks
    if (!has_selection) return nullptr;
    return UINode::hit_test(point);
}

void VehiclePanel::draw() {
    const UIStyle& s = style();

    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f)
        DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);

    for (int i = 0; i < VEHICLE_PANEL_LINE_COUNT; ++i) {
        const Vector2 at{
            screen_rect.x + VEHICLE_PANEL_GAP,
            screen_rect.y + VEHICLE_PANEL_GAP + static_cast<float>(i) * VEHICLE_PANEL_LINE_HEIGHT,
        };
        // The first line at full size, the two under it a little smaller
        const float size = i == 0 ? s.font_size : s.font_size * 0.8f;
        DrawTextEx(s.font, lines[i].c_str(), at, size, s.font_spacing, s.foreground);
    }
}

void VehiclePanel::draw_selection_highlight() const {
    const VehicleEntity* entity = entities->vehicle(selected);
    if (entity == nullptr) return;

    // Same approach as GridTransformMenu's highlight: back into the 3D scene,
    // with the depth buffer still holding it, and each box drawn with the
    // matrix its model was drawn with
    BeginMode3D(voxel_view->camera); {
        for (VoxelGrid* grid : entity->owned_grids()) {
            for (const ModelInfo* model_info : grid->get_models()) {
                if (model_info == nullptr || !model_info->do_render) continue;
                rlPushMatrix(); {
                    rlMultMatrixf(MatrixToFloat(voxel_model_matrix(grid, *model_info)));
                    // Taken when the mesh was built (ModelInfo::bounds)
                    if (model_info->model.meshCount > 0) DrawBoundingBox(model_info->bounds, VEHICLE_HIGHLIGHT_COLOUR);
                }
                rlPopMatrix();
            }
        }
    }
    EndMode3D();
}
