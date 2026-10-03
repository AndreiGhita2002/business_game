//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_ENTITY_HPP
#define BUSINESS_GAME_ENTITY_HPP
#include <memory>
#include <vector>

#include <raylib.h>

#include "entity/GridSink.hpp"
#include "entity/SimConvert.hpp"
#include "game/Script.hpp"
#include "sim/Simulation.hpp"

class VoxelGrid;

/**
 * A simulation object made visible: the voxel grids that stand in for it, and
 * whatever cosmetic behaviour goes with them.
 *
 * An entity is presentation and nothing else. It exists only while its
 * simulation object is near enough to the camera to be worth drawing (see
 * EntityManager), it is rebuilt from the simulation whenever it comes back,
 * and nothing it does ever reaches the simulation. That is what lets the
 * simulation run objects nobody is looking at, and objects with no visual
 * counterpart at all.
 *
 * Each subclass mirrors one kind of simulation object. on_tick() copies what
 * the simulation says after every step, and present() turns that into grid
 * transforms every frame, interpolating between ticks.
 *
 * The entity owns its grids: the ones its asset produced, recorded when it was
 * made. Grids attached to them later by someone else are not its own, and are
 * left standing when it goes.
 */
class Entity {
public:
    /**
     * Takes ownership of `root` and every grid below it, and hands them to the
     * sink to be drawn.
     * @param pivot: the point of the root grid's model space that place() puts
     *        on the pose's position.
     */
    Entity(GridSink* sink, VoxelGrid* root, Vector3 pivot);

    /** Takes its grids back out of the sink, then deletes them. */
    virtual ~Entity();

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    /** After every simulation step: copy what this tick says. */
    virtual void on_tick(const sim::Simulation& sim) = 0;

    /**
     * Every frame: place the grids and run the cosmetic scripts.
     * @param alpha: how far the frame is between the last two ticks, 0 to 1.
     * @param frame_dt: the frame's length in seconds, for the scripts.
     */
    virtual void present(float alpha, float frame_dt);

    /**
     * Gives the entity a cosmetic script, started straight away and run at the
     * end of every present(). Game logic does not belong in one - that is the
     * simulation's job - only things like a wheel turning.
     */
    void add_script(std::unique_ptr<Script> script);

    VoxelGrid* root_grid() const { return grids.front(); }

    /** Every grid the entity owns, parents before children. */
    const std::vector<VoxelGrid*>& owned_grids() const { return grids; }

protected:
    /** Moves the root grid so the pivot sits on `pose`, facing its way. */
    void place(const Pose& pose);

private:
    GridSink* sink;
    std::vector<VoxelGrid*> grids;
    Vector3 pivot;
    std::vector<std::unique_ptr<Script>> scripts;
};

#endif //BUSINESS_GAME_ENTITY_HPP
