//
// Created by Andrei Ghita on 04.10.2026.
//

#ifndef BUSINESS_GAME_GRIDSINK_HPP
#define BUSINESS_GAME_GRIDSINK_HPP
#include <vector>

class VoxelGrid;

/**
 * Wherever an entity's grids go to be updated and drawn: the VoxelView in the
 * game, a fake in the tests.
 *
 * An entity owns its grids but cannot draw them, and the VoxelView draws grids
 * but does not own an entity's. This is the narrow seam between the two, kept
 * as an interface so EntityManager can be tested without a window.
 */
class GridSink {
public:
    virtual ~GridSink() = default;

    /** Starts drawing these grids. Ownership stays with the caller. */
    virtual void add_grids(const std::vector<VoxelGrid*>& grids) = 0;

    /**
     * Stops drawing these grids. Called just before they are deleted, so this
     * is also where anything else holding a pointer to one has to let go.
     */
    virtual void remove_grids(const std::vector<VoxelGrid*>& grids) = 0;
};

#endif //BUSINESS_GAME_GRIDSINK_HPP
