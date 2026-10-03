//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_VEHICLEENTITY_HPP
#define BUSINESS_GAME_VEHICLEENTITY_HPP
#include <string>
#include <vector>

#include "entity/Entity.hpp"
#include "sim/Handle.hpp"

/**
 * A sim::Vehicle made visible.
 *
 * Keeps the vehicle's pose at the last two ticks and draws it between them,
 * so a simulation ticking at 20 Hz moves smoothly at any frame rate. The cost
 * is that what is drawn is up to one tick behind the simulation.
 */
class VehicleEntity final : public Entity {
public:
    /**
     * Built from the vehicle as it stands, with both poses set to it, so a new
     * entity never slides in from somewhere else on its first frame.
     */
    VehicleEntity(sim::VehicleId id, GridSink* sink, VoxelGrid* root, Vector3 pivot,
                  const sim::Simulation& sim);

    sim::VehicleId vehicle_id() const { return id; }

    void on_tick(const sim::Simulation& sim) override;
    void present(float alpha, float frame_dt) override;

    /** The pose at the newest tick. */
    const Pose& current_pose() const { return current; }

    /** The pose drawn on the last present(). */
    const Pose& shown_pose() const { return shown; }

    /**
     * How fast the vehicle is going, in world units per second, never
     * negative: a vehicle running its route backwards is turned round to face
     * the way it goes, so to its wheels it is always driving forwards.
     */
    float ground_speed() const { return speed; }

private:
    sim::VehicleId id;
    Pose previous;
    Pose current;
    Pose shown;
    float speed = 0.0f;

    // Reads the vehicle's pose and speed out of the simulation. False when the
    // vehicle no longer exists, in which case nothing is changed.
    bool read(const sim::Simulation& sim, Pose* pose);
};

/**
 * Turns every grid named WHEEL_GRID_NAME in a vehicle's tree as fast as the
 * vehicle is going, so the wheels roll rather than slide. A cosmetic script
 * reading simulation state through its entity, which is the pattern for all
 * of them.
 *
 * The wheels are attached grids, so each is turned about its own axle (model
 * Z, which is grid y) and then snapped back onto its anchor: a turn about the
 * grid's own origin would carry its connector away otherwise.
 */
class WheelSpinScript final : public Script {
public:
    WheelSpinScript(const VehicleEntity* vehicle, std::vector<VoxelGrid*> wheels, float radius);

    void on_update(float delta) override;
    const std::string& get_type() const override;

private:
    const VehicleEntity* vehicle;
    std::vector<VoxelGrid*> wheels;
    float radius;
    // The wheels' angle, kept here and written whole every frame rather than
    // added onto the grid's rotation, so rounding cannot build up
    float angle = 0.0f;
};

/** The grids in `grids` named WHEEL_GRID_NAME. */
std::vector<VoxelGrid*> find_wheels(const std::vector<VoxelGrid*>& grids);

#endif //BUSINESS_GAME_VEHICLEENTITY_HPP
