//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_VEHICLEPANEL_HPP
#define BUSINESS_GAME_VEHICLEPANEL_HPP
#include <functional>
#include <string>

#include "sim/Command.hpp"
#include "sim/Handle.hpp"
#include "sim/Simulation.hpp"
#include "ui/UINode.hpp"

#define VEHICLE_PANEL_STR "VehiclePanel"

// Layout, in pixels
#define VEHICLE_PANEL_WIDTH 380.0f
#define VEHICLE_PANEL_GAP 6.0f
#define VEHICLE_PANEL_LINE_HEIGHT 22.0f
#define VEHICLE_PANEL_LINE_COUNT 3
#define VEHICLE_PANEL_BUTTON_HEIGHT 26.0f
#define VEHICLE_PANEL_BUTTON_COUNT 4

class EntityManager;
class VoxelView;

/**
 * What a vehicle is doing, and the buttons that tell it to do something else.
 *
 * Click a car in the world (while no other tool is waiting on a click) and the
 * panel shows what the simulation says about it. The buttons never touch the
 * vehicle: each queues a command, which the simulation applies at the next
 * tick, and the panel shows the result once it has. That round trip - UI to
 * command to simulation to entity - is the whole path any player action takes.
 *
 * Hidden while nothing is selected. The selection is a simulation handle, not
 * an entity, so it survives the car driving out of range and back.
 */
class VehiclePanel final : public UINode {
public:
    /**
     * Whether another tool owns the next world click (the voxel editor with a
     * colour armed, the transform menu waiting on a grid). Set by whoever
     * builds the UI. The panel only picks when this says no.
     */
    std::function<bool()> world_click_taken;

    std::string& get_view_type() override;

    void update(float delta_time) override;
    void render() override;
    UINode* hit_test(Vector2 point) override;

    void draw() override;
    Vector2 measure() override;

    void select(sim::VehicleId id);
    void clear_selection();

    /**
     * @param sim: read only, for what to show.
     * @param commands: where the buttons' commands go.
     */
    VehiclePanel(ViewNode* parent, VoxelView* voxel_view, EntityManager* entities,
                 const sim::Simulation* sim, sim::CommandQueue* commands);

private:
    VoxelView* voxel_view;
    EntityManager* entities;
    const sim::Simulation* sim;
    sim::CommandQueue* commands;

    bool has_selection;
    sim::VehicleId selected;

    // The text, refreshed every update while something is selected
    std::string lines[VEHICLE_PANEL_LINE_COUNT];

    // Picks the vehicle under the cursor on a free left click
    void pick_from_world();
    void refresh_lines();

    // What the buttons do. Each reads the vehicle's speed as the simulation
    // has it now and queues the new one.
    void faster() const;
    void slower() const;
    void reverse() const;
    void remove() const;

    // Boxes around the selected vehicle's grids, while it has an entity
    void draw_selection_highlight() const;
};

#endif //BUSINESS_GAME_VEHICLEPANEL_HPP
