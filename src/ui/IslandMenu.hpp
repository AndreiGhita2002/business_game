//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_ISLANDMENU_HPP
#define BUSINESS_GAME_ISLANDMENU_HPP
#include <functional>
#include <string>

#include "sim/Island.hpp"
#include "ui/UINode.hpp"

class UIButton;
class UILabel;
class VoxelView;

namespace sim { class Simulation; }

#define ISLAND_MENU_STR "IslandMenu"

/**
 * Choosing an island and putting it in the world.
 *
 * Opened by the "New Island" button: a panel of the island's attributes
 * (shape, turn, elevation, biome, the seed of its noise), each a button that
 * cycles through its choices, with "Random" to draw them all at once from a
 * fresh seed. "Place" then lets the island follow the mouse over the sea: the
 * cells its footprint would cover are highlighted, green where it fits and
 * red where it hangs off the world or over land, R turns it, a left click
 * puts it there and Esc or a right click goes back to the choices. The panel
 * stays up while placing, so a choice can still be changed and is seen at
 * once.
 *
 * Placing does not change anything itself: it hands the cell and the spec to
 * `on_place`, which queues a sim::PlaceIsland. The island appears a tick
 * later, when the simulation has made it.
 */
class IslandMenu final : public UINode {
public:
    enum class Stage { Closed, Choosing, Placing };

    // What the island will be. Edited by the panel's buttons.
    sim::IslandSpec spec;

    // Called as placing starts, so the other world click tools can stand down
    // (the same arrangement the editor and the transform menu have)
    std::function<void()> on_activate;
    // Called with the footprint's corner cell and the spec on a click that
    // places the island. main.cpp queues the command.
    std::function<void(int32_t cell_x, int32_t cell_y, const sim::IslandSpec& spec)> on_place;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;
    UINode* hit_test(Vector2 point) override;
    void draw() override;
    Vector2 measure() override;

    /** Opens the panel with a random island chosen, or closes it if it is open. */
    void toggle();
    /** Back to the choices, if placing. The panel stays open. */
    void cancel_placing();
    /** Closes the panel, placing or not. */
    void close();

    bool is_open() const { return stage != Stage::Closed; }
    bool is_placing() const { return stage == Stage::Placing; }

    /**
     * @param sim: read for which cells are land. Borrowed: the game assigns a
     *        new world into the same Simulation, so the pointer stays good.
     */
    IslandMenu(ViewNode* parent, VoxelView* voxel_view, const sim::Simulation* sim);

private:
    VoxelView* voxel_view;
    const sim::Simulation* sim;
    Stage stage = Stage::Closed;

    UIButton* shape_button;
    UIButton* turn_button;
    UIButton* elevation_button;
    UIButton* biome_button;
    UIButton* seed_button;
    UIButton* place_button;
    UILabel* hint;

    // Where the footprint would go, under the mouse, while placing
    bool has_hover = false;
    sim::CellPos corner{};
    bool fits = false;

    void start_placing();
    void randomise();
    void refresh_text();
    // The cell under the mouse, where a ray from the camera meets the water's
    // surface. False when the ray never comes down to the sea.
    bool cell_under_mouse(sim::CellPos* out) const;
};

#endif //BUSINESS_GAME_ISLANDMENU_HPP
