//
// Created by Claude on 09.10.2026.
//

#include "ui/IslandMenu.hpp"

#include <cmath>
#include <memory>
#include <random>
#include <raymath.h>
#include <rlgl.h>

#include "entity/TerrainVoxels.hpp"
#include "game/Transform.hpp"
#include "sim/Simulation.hpp"
#include "ui/UIButton.hpp"
#include "ui/UILabel.hpp"
#include "ui/UIView.hpp"
#include "voxel/VoxelView.hpp"
#include "water/WaterView.hpp"

namespace {

// The panel's layout, in pixels
constexpr float PANEL_WIDTH = 260.0f;
constexpr float PANEL_PADDING = 8.0f;
constexpr float ROW_HEIGHT = 32.0f;
constexpr float ROW_GAP = 8.0f;
constexpr int ROW_COUNT = 9;

// The footprint's highlight: translucent, green where the island fits and red
// where it does not, with a solid outline round each cell
constexpr Color FITS_FILL{80, 220, 120, 90};
constexpr Color FITS_EDGE{40, 160, 80, 255};
constexpr Color BLOCKED_FILL{230, 80, 80, 90};
constexpr Color BLOCKED_EDGE{180, 40, 40, 255};
// How far above the water the highlight stands, in voxels
constexpr float HIGHLIGHT_ABOVE_WATER = 6.0f;

float row_y(const int row) {
    return PANEL_PADDING + static_cast<float>(row) * (ROW_HEIGHT + ROW_GAP);
}

Rectangle row_rect(const int row) {
    return Rectangle{PANEL_PADDING, row_y(row), PANEL_WIDTH - 2.0f * PANEL_PADDING, ROW_HEIGHT};
}

uint32_t fresh_seed() {
    return std::random_device{}();
}

} // namespace

std::string& IslandMenu::get_view_type() {
    static std::string TYPE = ISLAND_MENU_STR;
    return TYPE;
}

IslandMenu::IslandMenu(ViewNode* parent, VoxelView* voxel_view, const sim::Simulation* sim)
    : UINode(parent, Rectangle{16.0f, 0.0f, 0.0f, 0.0f}, Anchor::CENTER_LEFT),
      voxel_view(voxel_view), sim(sim)
{
    auto title = std::make_unique<UILabel>(this, "New Island", row_rect(0), Anchor::TOP_LEFT);
    title->font_size = 24.0f;
    add_child(std::move(title));

    // Each choice is a button that steps on to the next one when it is clicked
    const auto add_button = [this](const int row, std::function<void()> action) {
        auto button = std::make_unique<UIButton>(this, "", std::move(action), row_rect(row), Anchor::TOP_LEFT);
        UIButton* raw = button.get();
        add_child(std::move(button));
        return raw;
    };
    shape_button = add_button(1, [this] {
        spec.shape = static_cast<sim::IslandShape>((static_cast<int>(spec.shape) + 1) % sim::ISLAND_SHAPE_COUNT);
    });
    turn_button = add_button(2, [this] { spec.rotation = static_cast<uint8_t>((spec.rotation + 1) % 4); });
    elevation_button = add_button(3, [this] {
        spec.elevation = static_cast<sim::Elevation>((static_cast<int>(spec.elevation) + 1) % sim::ELEVATION_COUNT);
    });
    biome_button = add_button(4, [this] {
        spec.biome = static_cast<sim::Biome>((static_cast<int>(spec.biome) + 1) % sim::BIOME_COUNT);
    });
    // The noise's seed, which is everything about the land's shape that the
    // rows above do not say. A click rolls a new one.
    seed_button = add_button(5, [this] { spec.seed = fresh_seed(); });
    add_button(6, [this] { randomise(); })->text = "Random";

    // Place and Cancel share a row
    const float half = (PANEL_WIDTH - 2.0f * PANEL_PADDING - ROW_GAP) * 0.5f;
    auto place = std::make_unique<UIButton>(this, "Place", [this] {
        if (stage == Stage::Placing) cancel_placing();
        else start_placing();
    }, Rectangle{PANEL_PADDING, row_y(7), half, ROW_HEIGHT}, Anchor::TOP_LEFT);
    place_button = place.get();
    add_child(std::move(place));
    add_child(std::make_unique<UIButton>(this, "Cancel", [this] { close(); },
        Rectangle{PANEL_PADDING + half + ROW_GAP, row_y(7), half, ROW_HEIGHT}, Anchor::TOP_LEFT));

    auto hint_label = std::make_unique<UILabel>(this, "", row_rect(8), Anchor::TOP_LEFT);
    hint_label->font_size = 16.0f;
    hint = hint_label.get();
    add_child(std::move(hint_label));

    randomise();
}

void IslandMenu::toggle() {
    if (is_open()) {
        close();
        return;
    }
    randomise();
    stage = Stage::Choosing;
}

void IslandMenu::close() {
    stage = Stage::Closed;
    has_hover = false;
}

void IslandMenu::cancel_placing() {
    if (stage == Stage::Placing) stage = Stage::Choosing;
    has_hover = false;
}

void IslandMenu::start_placing() {
    // The other tools first, so whatever on_activate does cannot undo this
    if (on_activate) on_activate();
    stage = Stage::Placing;
}

void IslandMenu::randomise() {
    spec = sim::random_island_spec(fresh_seed());
}

void IslandMenu::refresh_text() {
    shape_button->text = TextFormat("Shape: %s", sim::island_shape_name(spec.shape));
    turn_button->text = TextFormat("Turn: %i", spec.rotation * 90);
    elevation_button->text = TextFormat("Elevation: %s", sim::elevation_name(spec.elevation));
    biome_button->text = TextFormat("Biome: %s", sim::biome_name(spec.biome));
    seed_button->text = TextFormat("Seed: %u", spec.seed);
    place_button->text = stage == Stage::Placing ? "Back" : "Place";

    if (stage != Stage::Placing) hint->text = "Choose, then Place";
    else if (!has_hover) hint->text = "Point at the sea";
    else hint->text = fits ? "Click to place, R turns" : "Does not fit here";
}

bool IslandMenu::cell_under_mouse(sim::CellPos* out) const {
    if (voxel_view == nullptr || voxel_view->game_map == nullptr || sim == nullptr) return false;

    // The ray carried into the map's own space (X grid x, Y up, Z grid y),
    // where the cells are, and met with the water's surface there
    const Ray ray = GetScreenToWorldRay(GetMousePosition(), voxel_view->camera);
    const Matrix inverse = MatrixInvert(transform_to_matrix(voxel_view->game_map->get_world_transform()));
    const Vector3 origin = Vector3Transform(ray.position, inverse);
    const Vector3 dir = Vector3Subtract(Vector3Transform(Vector3Add(ray.position, ray.direction), inverse), origin);
    if (std::fabs(dir.y) < 1e-6f) return false;

    const float surface = water_surface_height(sim->water_level());
    const float t = (surface - origin.y) / dir.y;
    if (t < 0.0f) return false;
    const Vector3 at = Vector3Add(origin, Vector3Scale(dir, t));

    const float cell_voxels = static_cast<float>(sim->terrain().cell_blocks() * BLOCK_VOXELS);
    if (cell_voxels <= 0.0f) return false;
    *out = sim::CellPos{static_cast<int32_t>(std::floor(at.x / cell_voxels)),
                        static_cast<int32_t>(std::floor(at.z / cell_voxels))};
    return true;
}

void IslandMenu::update(const float delta_time) {
    if (isEnabled && stage == Stage::Placing) {
        // Back to the choices on Esc or a right click on the world
        const UIView* ui = get_view();
        const bool mouse_free = ui == nullptr || !ui->mouse_consumed;
        if (IsKeyPressed(KEY_ESCAPE) || (mouse_free && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT))) {
            cancel_placing();
        } else {
            if (IsKeyPressed(KEY_R)) spec.rotation = static_cast<uint8_t>((spec.rotation + 1) % 4);

            // The footprint centred on the cell under the mouse, as near as
            // a whole number of cells allows
            sim::CellPos hover{};
            has_hover = mouse_free && cell_under_mouse(&hover);
            if (has_hover) {
                const sim::Footprint footprint = sim::island_footprint(spec.shape, spec.rotation);
                corner = sim::CellPos{hover.x - (footprint.width - 1) / 2, hover.y - (footprint.height - 1) / 2};
                fits = true;
                for (const sim::CellPos& c : footprint.cells) {
                    if (!sim->terrain().is_ocean_cell(corner.x + c.x, corner.y + c.y)) fits = false;
                }

                if (fits && mouse_free && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (on_place) on_place(corner.x, corner.y, spec);
                    close();
                }
            }
        }
    }
    if (is_open()) refresh_text();

    ViewNode::update(delta_time);
}

void IslandMenu::render() {
    if (!isEnabled) return;

    // Only the panel and its buttons are held back while it is closed; the
    // sibling chain is still walked, as a SettingsPanel does
    if (is_open()) {
        draw();
        if (child) child->render();
    }
    if (sibling) sibling->render();
}

UINode* IslandMenu::hit_test(const Vector2 point) {
    // A closed panel cannot be clicked, and must not swallow world clicks
    if (!is_open()) return nullptr;
    return UINode::hit_test(point);
}

Vector2 IslandMenu::measure() {
    return Vector2{PANEL_WIDTH, row_y(ROW_COUNT) - ROW_GAP + PANEL_PADDING};
}

void IslandMenu::draw() {
    const UIStyle& s = style();
    DrawRectangleRec(screen_rect, s.background);
    if (s.border_thickness > 0.0f) DrawRectangleLinesEx(screen_rect, s.border_thickness, s.border);

    if (stage != Stage::Placing || !has_hover || sim == nullptr || voxel_view == nullptr) return;

    // The footprint over the sea, a box per cell from the sea floor to just
    // above the water. It belongs to the 3D scene, so a camera block is
    // opened again on top of it, in the map's own space so it sits on the
    // cells wherever the map is.
    const sim::Terrain& terrain = sim->terrain();
    const float cell_voxels = static_cast<float>(terrain.cell_blocks() * BLOCK_VOXELS);
    const float bottom = static_cast<float>(terrain.sea_floor() * BLOCK_VOXELS);
    const float top = water_surface_height(sim->water_level()) + HIGHLIGHT_ABOVE_WATER;
    const sim::Footprint footprint = sim::island_footprint(spec.shape, spec.rotation);

    BeginMode3D(voxel_view->camera); {
        rlPushMatrix(); {
            rlMultMatrixf(MatrixToFloat(transform_to_matrix(voxel_view->game_map->get_world_transform())));
            for (const sim::CellPos& c : footprint.cells) {
                const Vector3 centre{
                    (static_cast<float>(corner.x + c.x) + 0.5f) * cell_voxels,
                    (bottom + top) * 0.5f,
                    (static_cast<float>(corner.y + c.y) + 0.5f) * cell_voxels,
                };
                DrawCube(centre, cell_voxels, top - bottom, cell_voxels, fits ? FITS_FILL : BLOCKED_FILL);
                DrawCubeWires(centre, cell_voxels, top - bottom, cell_voxels, fits ? FITS_EDGE : BLOCKED_EDGE);
            }
        }
        rlPopMatrix();
    }
    EndMode3D();
}
