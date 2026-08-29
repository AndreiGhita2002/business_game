//
// Created by Andrei Ghita on 16.09.2025.
//

#ifndef BUSINESS_GAME_VOXELGRID_HPP
#define BUSINESS_GAME_VOXELGRID_HPP
#include <raylib.h>
#include <array>
#include <iosfwd>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// #include "voxel/VoxelView.hpp"
class VoxelView;
class VoxelGrid;

// REMINDER: Z goes UP/DOWN

inline Transform identity();

#define CHUNK_SIZE 16
using VoxelID = uint8_t;
using VoxelChunk = std::array<VoxelID,CHUNK_SIZE*CHUNK_SIZE*CHUNK_SIZE>;
using VoxelColourMap = std::shared_ptr<std::map<VoxelID, Color>>;

struct Int2 {
    int x;
    int y;

    bool operator<(const Int2& other) const noexcept {
        if (x < other.x) return true;
        if (x > other.x) return false;
        return y < other.y;
    }

    bool operator==(const Int2& other) const noexcept {
        return x == other.x && y == other.y;
    }
};

struct Int3 {
    int x;
    int y;
    int z;

    bool operator<(const Int3& other) const noexcept {
        if (x < other.x) return true;
        if (x > other.x) return false;
        if (y < other.y) return true;
        if (y > other.y) return false;
        return z < other.z;
    }

    bool operator==(const Int3& other) const noexcept {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct ModelInfo {
    bool do_render;
    Model model;
    Transform transform;
};

/**
 * How one grid is held onto another: the grid it hangs off, plus the voxel on
 * each side that holds the two together.
 *
 * A wheel attached to a car is a grid whose connector is the voxel at the hub,
 * held to the voxel on the car that is the end of the axle. The wheel moves
 * with the car because it is a child of it, and turns on its own because its
 * own local transform is still its to change.
 *
 * Neither connector voxel can be cleared while the attachment stands, see
 * VoxelGrid::set_voxel.
 */
struct Attachment {
    // The grid this one hangs off. Always the same as the parent, as
    // attach_to() sets both, and detach() clears both.
    VoxelGrid* anchor = nullptr;
    // The connector voxel in the anchor's grid coordinates
    Int3 anchor_voxel{};
    // The connector voxel in the attached grid's own coordinates
    Int3 local_voxel{};
};

class VoxelGrid {
public:
    // Where this grid sits relative to its parent, or relative to the world
    // when it has none. Anything that draws, picks or measures a distance wants
    // get_world_transform() instead, which folds the parents in.
    Transform transform;
    VoxelColourMap voxel_colours;

    // What a saved file carries in its readable header. Empty for a grid that
    // was built in code and never saved or loaded. See VoxelFile.hpp.
    std::string name;
    std::string description;

    virtual std::string& get_grid_type() = 0;
    virtual Int2 get_size() = 0;
    virtual VoxelID* get_voxel(Int3 grid_pos) = 0;
    virtual void update_models() = 0;
    //TODO (optimisation)
    // this function could be improved by having it not create
    // a new vector every time. Ideally, there should be a linked
    // list where every node has an array of ModelInfos that are
    // managed by their respective grid
    virtual std::vector<ModelInfo*> get_models() = 0;

    // --- Transforms ---
    // Every grid handles these the same way, so none of them are virtual.

    /** Where the grid sits relative to its parent. What saving writes out. */
    Transform get_transform() const { return transform; }
    void set_transform(const Transform new_transform) { transform = new_transform; }

    /**
     * Where the grid sits in the world: its own transform with every parent's
     * applied on top of it, up to the root. This is the one that rendering,
     * picking and render distance checks go through, so that moving a parent
     * moves everything hanging off it.
     *
     * Worked out on every call rather than cached, as the chain is short and a
     * cache would go stale the moment someone wrote to `transform` directly.
     * Still worth holding on to the result rather than calling it per model.
     */
    Transform get_world_transform() const;

    // --- Hierarchy ---
    // The links are not owning: a grid is owned by whatever created it (the
    // VoxelView holds them all in voxel_grids), and these only say what moves
    // with what.

    VoxelGrid* get_parent() const { return parent; }
    const std::vector<VoxelGrid*>& get_children() const { return children; }

    /**
     * Hangs this grid off another one, so that moving the parent moves this
     * grid with it. The local transform is left alone, so the grid moves to
     * wherever the parent puts it. Pass nullptr to detach.
     *
     * Refuses a parent that is this grid or already below it, which would make
     * get_world_transform() recurse forever.
     * @return false when the parent was refused.
     */
    bool set_parent(VoxelGrid* new_parent);

    /** set_parent() the other way round. */
    bool add_child(VoxelGrid* child);

    /** Whether this grid is somewhere above `other` in the tree. */
    bool is_ancestor_of(const VoxelGrid* other) const;

    // --- Attachment ---
    // Attaching is the hierarchy plus a pair of voxels: it is how a grid is
    // built onto another one rather than merely carried by it.

    /**
     * Holds this grid onto another one at a voxel on each side.
     *
     * The grid becomes a child of the anchor, so the anchor's transform is
     * applied to it and not the other way round, and by default it is moved so
     * that the two connector voxels sit in the same place. What it does with
     * its own local transform afterwards is still its own business, which is
     * how a wheel turns while the car it is attached to drives off.
     *
     * Both connectors have to be solid voxels that exist in their grid, and an
     * anchor that is already below this grid is refused, as that would make
     * get_world_transform() recurse forever. Attaching a grid that is already
     * attached moves it to the new anchor.
     *
     * @param anchor: the grid to hang off.
     * @param anchor_voxel: the connector, in the anchor's coordinates.
     * @param local_voxel: the connector, in this grid's coordinates.
     * @param snap: whether to move the grid so the connectors line up. Pass
     *        false to keep the local transform exactly as it is.
     * @return false when nothing was changed.
     */
    bool attach_to(VoxelGrid* anchor, Int3 anchor_voxel, Int3 local_voxel, bool snap = true);

    /**
     * Undoes attach_to(): the grid stops being a child of its anchor and takes
     * the anchor's own parent instead, so it moves one step up the tree rather
     * than out of it. It keeps the place it was in, and both connector voxels
     * become ordinary voxels again.
     * @return false when the grid was not attached.
     */
    bool detach();

    bool is_attached() const { return attachment.has_value(); }

    /** What this grid is attached to, or nullptr when it is not attached. */
    const Attachment* get_attachment() const {
        return attachment.has_value() ? &attachment.value() : nullptr;
    }

    /**
     * Whether the voxel at this coordinate is holding an attachment together,
     * either as this grid's own connector or as the anchor voxel of a grid
     * attached to it. Those voxels cannot be cleared, see set_voxel().
     */
    bool is_connector_voxel(Int3 grid_pos) const;

    /**
     * Moves the grid so its connector voxel sits on the anchor's connector
     * voxel, leaving its rotation and scale alone. attach_to() does this once;
     * call it again after turning or scaling an attached grid, as a rotation
     * about the grid's own origin carries the connector away from the anchor.
     * @return false when the grid is not attached.
     */
    bool snap_to_anchor();

    /**
     * Writes everything about this grid that the file's header and common
     * section do not already carry, so its own size and its voxels.
     *
     * The layout is up to each grid - a chunked grid writes chunks, a grid with
     * one block of voxels writes one - but every voxel must go through
     * voxel_file::write_chunk, so that voxels have the same format everywhere.
     * Returns false on a write error.
     */
    virtual bool write_body(std::ostream& out) = 0;

    /**
     * Writes a voxel and marks whatever has to be meshed again.
     *
     * Clearing a connector voxel is refused while the attachment that needs it
     * is still there, whether it is this grid's own connector or the one an
     * attached grid is holding onto (see attach_to). Painting a connector a
     * different colour is fine, only taking it away is not.
     *
     * Returns false when the position falls outside the grid, or when the
     * write was refused.
     */
    bool set_voxel(Int3 grid_pos, VoxelID id);

    /** Whether a coordinate is inside this grid at all. */
    virtual bool in_bounds(Int3 grid_pos) const = 0;

    /** Whether the grid holds anything other than air at this coordinate. */
    bool is_solid(Int3 grid_pos);

    /**
     * The middle of a voxel in the grid's own space, which is the space a
     * model is placed in. That space is the mesher's: X is grid x, Y is grid z
     * (up) and Z is grid y, one unit per voxel.
     */
    static Vector3 voxel_centre_local(Int3 grid_pos);

    /**
     * Turns a point in the local space of one of this grid's models into the
     * coordinate of the voxel that contains it. Model space is the space the
     * mesher builds in, so X is grid x, Y is grid z (up) and Z is grid y.
     * Returns false when the model does not belong to this grid.
     */
    virtual bool model_to_grid(const ModelInfo* model, Vector3 local_pos, Int3* out) = 0;

    explicit VoxelGrid(VoxelView* view) : transform(identity()), view(view) {}
    virtual ~VoxelGrid();
protected:
    // The view responsible for drawing this grid;
    VoxelView* view;

    // Whatever this grid hangs off, and whatever hangs off it. Neither is owned.
    VoxelGrid* parent = nullptr;
    std::vector<VoxelGrid*> children;

    // Set while this grid is attached to another one. The anchor in it is
    // always the parent above; set_parent() drops the attachment rather than
    // let the two disagree. A grid does not track what is attached *to* it:
    // that is read off the children, so there is only ever one copy of it.
    std::optional<Attachment> attachment;

    /**
     * set_voxel() once it has agreed to the write: the grid's own storage and
     * whatever it has to mark for meshing again. Bounds are still this
     * function's to check, as only the grid knows its own shape.
     */
    virtual bool write_voxel(Int3 grid_pos, VoxelID id) = 0;

    /** Drops a child from `children` without touching the child itself. */
    void forget_child(const VoxelGrid* child);

    // helper: floor division/modulo that work for negatives
    static int floordiv(const int a, const int b) {
        int q = a / b;
        int r = a % b;
        return (r && ((r > 0) != (b > 0))) ? (q - 1) : q;
    }
    static int floormod(const int a, const int b) {
        int m = a % b;
        return (m < 0) ? (m + (b > 0 ? b : -b)) : m;
    }
};


inline Transform identity() {
    return Transform{
        Vector3 {0.0, 0.0, 0.0},
        Quaternion {0.0, 0.0, 0.0, 1.0},
        Vector3 {1.0, 1.0, 1.0},
    };
}

#endif //BUSINESS_GAME_VOXELGRID_HPP