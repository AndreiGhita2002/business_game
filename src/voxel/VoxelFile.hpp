//
// Created by Andrei Ghita on 26.08.2026.
//

#ifndef BUSINESS_GAME_VOXELFILE_HPP
#define BUSINESS_GAME_VOXELFILE_HPP
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

#include "voxel/VoxelGrid.hpp"

class VoxelView;

/**
 * On disk format for voxel grids.
 *
 * A file holds one grid and everything hanging off it, flattened: the magic and
 * how many grids there are, then every grid's readable header, then every grid's
 * binary body in the same order.
 *
 *   BGVOX 3                      <- magic and format version, always line one
 *   grid_count: 2
 *   voxel_bytes: 1
 *   chunk_size: 16
 *
 *   --- GRID ---                 <- one block per grid, in the order below
 *   id: 0
 *   type: VoxelMap               <- picks the loader, see load_grid()
 *   name: my map
 *   description: the world
 *   parent: none
 *   children: 1
 *   palette_size: 12
 *
 *   --- GRID ---
 *   id: 1
 *   type: SingleChunkGrid
 *   name: crate
 *   description: sits on the map
 *   parent: 0
 *   children:
 *   anchor_voxel: 8 3 2          <- attached to the parent, at this voxel of it
 *   connector_voxel: 1 1 1       <- held there by this voxel of its own
 *   palette_source: parent       <- meshed with the parent's colours, so its
 *   palette_size: 0                 body carries no palette of its own
 *
 *   --- BINARY ---               <- everything past this line's newline is binary
 *   <body><body>
 *
 * Header lines are "key: value", in any order inside their block. Blank lines
 * and lines starting with '#' are ignored, unknown keys are kept in `fields` so
 * an older reader does not lose them.
 *
 * The ids mean nothing outside the file: the writer numbers the grids from 0 as
 * it walks the tree. `parent` and `children` point at those numbers and are
 * what the tree is put back together from, so the bodies carry no structure at
 * all.
 *
 * An attachment (VoxelGrid::attach_to) is that parent link plus a connector
 * voxel on each side, so only the two voxels are written: `anchor_voxel` in the
 * parent's coordinates and `connector_voxel` in the grid's own. Both lines
 * together or neither - a grid without them comes back merely hanging off its
 * parent, which is what every file written before this carries.
 *
 *   <body>      := u32 id, u32 payload_bytes, <payload>
 *   <payload>   := [<palette>] <transform> <grid body>
 *   <palette>   := u32 count, then that many { u8 id, u8 r, u8 g, u8 b, u8 a }
 *                  left out when the header says palette_source: parent
 *   <transform> := f32 x10: translation xyz, rotation xyzw, scale xyz. Local,
 *                  so relative to the parent grid.
 *
 * Each body naming its own id, and carrying its own length, is what lets the
 * loader cope with bodies that are not in header order: they are indexed first
 * and then read by id. That should never happen, and is logged when it does.
 *
 * The grid body is up to each grid (VoxelGrid::write_body), but the voxels
 * inside it always go through write_chunk()/read_chunk(), so every grid stores
 * voxels the same way no matter how it lays out the rest.
 *
 * All integers are little endian, floats are IEEE 754 bit patterns written as
 * u32, so a file written on one machine reads on another.
 */
namespace voxel_file {

// First line of every file: MAGIC, a space, then the format version.
inline constexpr char MAGIC[] = "BGVOX";
inline constexpr int FORMAT_VERSION = 3;
// Opens each grid's header block.
inline constexpr char GRID_MARKER[] = "--- GRID ---";
// The line that closes the readable part of the file.
inline constexpr char BINARY_MARKER[] = "--- BINARY ---";
// The extension these files are expected to carry.
inline constexpr char FILE_EXTENSION[] = ".bgvox";

/** One grid's readable header block. */
struct GridHeader {
    // Only means anything inside the file it came from.
    uint32_t id = 0;
    std::string grid_type;
    std::string name;
    std::string description;

    // The id of the grid this one hangs off. A file has one grid with no
    // parent, and that is the one load_grid() returns.
    bool has_parent = false;
    uint32_t parent_id = 0;
    // The ids of the grids hanging off this one. Says the same as the parent
    // fields the other way round, and either one is enough to rebuild the tree.
    std::vector<uint32_t> children;

    // Whether the grid is attached to its parent rather than only hanging off
    // it, and the two connector voxels holding them together: anchor_voxel in
    // the parent's coordinates, connector_voxel in this grid's own.
    bool has_attachment = false;
    Int3 anchor_voxel{};
    Int3 connector_voxel{};

    // Whether the grid shares its parent's colour map, in which case its body
    // carries no palette.
    bool palette_from_parent = false;

    // Every "key: value" line of the block, including the ones mirrored above
    // and any key this build does not know about.
    std::map<std::string, std::string> fields;

    /** The value of a key, or nullptr when the block did not carry it. */
    const std::string* find(const std::string& key) const;
    /** The value of a key as an int, or fallback when missing or unparsable. */
    int get_int(const std::string& key, int fallback) const;
};

/**
 * The readable part of a file. Cheap to read on its own, so a file browser can
 * list a directory without touching any voxel data.
 */
struct Header {
    int version = FORMAT_VERSION;
    // The keys before the first grid block, so the ones about the file itself.
    std::map<std::string, std::string> fields;
    // In the order they appear in the file, which is the order the bodies are
    // meant to be in.
    std::vector<GridHeader> grids;

    // The root grid's name, description and type, copied out for listing files
    // without having to go looking for the root.
    std::string name;
    std::string description;
    std::string grid_type;

    const std::string* find(const std::string& key) const;
    int get_int(const std::string& key, int fallback) const;

    /** The first grid with no parent, or nullptr when the file holds none. */
    const GridHeader* root() const;
    /** The block with this id, or nullptr. */
    const GridHeader* find_grid(uint32_t id) const;
};

/**
 * Everything a grid specific loader is handed once the parts of the file that
 * are common to all grids have been read.
 */
struct LoadContext {
    VoxelView* view;
    // The whole file's readable part, and this grid's own block of it.
    const Header* file;
    const GridHeader* header;
    // The palette from this grid's body, the parent's when it shares one, or
    // the shared one the caller asked for.
    VoxelColourMap palette;
    // The grid's local transform, applied by load_grid() once the loader has
    // returned.
    Transform transform;
};

/**
 * Builds a grid from the body of a file. The stream is positioned at the first
 * byte of the grid's own body, after the palette and transform in front of it.
 * Returns nullptr on a malformed body, and ownership of the grid passes to the
 * caller otherwise.
 */
using GridLoader = VoxelGrid* (*)(std::istream& in, const LoadContext& ctx);

/**
 * Teaches load_grid() about a grid type. VoxelMap and SingleChunkGrid are
 * registered already; this is for grid types added later.
 * Returns false when the type name is already taken.
 */
bool register_grid_loader(const std::string& grid_type, GridLoader loader);

/** Whether a loader is registered for this grid type. */
bool can_load_grid_type(const std::string& grid_type);

/**
 * Writes a grid to a file, creating parent directories as needed.
 *
 * Everything hanging off the grid goes in with it, to any depth. The grids are
 * numbered from 0 as the tree is walked, headers are written in that order and
 * the bodies follow in the same order.
 *
 * The name and description are stored on the grid as well, so a later save
 * without them keeps what was used here.
 * Returns false and logs on any failure.
 */
bool save_grid(VoxelGrid* grid, const std::string& path,
               const std::string& name, const std::string& description);

/** Saves using the name and description the grid already carries. */
bool save_grid(VoxelGrid* grid, const std::string& path);

/** Reads only the readable part of a file, so every grid's header. */
bool read_header(const std::string& path, Header* out);

/**
 * Reads the headers, builds a grid per body through the loader registered for
 * its type, and hangs them off each other as the parent and children fields
 * say, so the whole tree comes back in the shape it was saved in. A grid whose
 * header carries connector voxels is attached to its parent afterwards, where
 * it was saved rather than snapped onto it again.
 *
 * Nothing is added to a VoxelView by this: the grids still have to be put in
 * VoxelView::voxel_grids to be updated and drawn, children included, which is
 * what collect_grids() is for.
 *
 * @param path: the file to read.
 * @param view: the view the new grids are drawn by.
 * @param shared_palette: when set, every grid in the file uses this palette and
 *        the ones in the file are dropped. Pass the scene's palette to keep
 *        everything on the same colours, or nullptr to use the file's own.
 * @param out_header: filled in with the headers when not null.
 * @return the root grid, owned by the caller, or nullptr on any failure. A
 *         failure part way through leaves nothing behind.
 */
VoxelGrid* load_grid(const std::string& path, VoxelView* view,
                     VoxelColourMap shared_palette = nullptr,
                     Header* out_header = nullptr);

/**
 * Appends `root` and everything below it to `out`, parents before children.
 * The order the grids are numbered and written in, and the usual way to hand a
 * freshly loaded tree to a VoxelView.
 */
void collect_grids(VoxelGrid* root, std::vector<VoxelGrid*>* out);

/**
 * Deletes a grid and everything below it, children first. The hierarchy links
 * do not own anything, so deleting the root on its own would leave the children
 * behind as roots of their own.
 */
void delete_grid_tree(VoxelGrid* root);

// --- The voxel format, shared by every grid ---

/**
 * Writes one chunk of voxels:
 *   u8  encoding      0 raw, 1 run length
 *   u32 payload_bytes number of bytes that follow, so a reader can skip a
 *                     chunk it does not want
 *   raw: CHUNK_SIZE^3 voxel ids, indexed x + y*CHUNK_SIZE + z*CHUNK_SIZE^2
 *   rle: { u16 run, u8 id } pairs covering exactly CHUNK_SIZE^3 voxels
 * Whichever of the two is smaller is written; readers must handle both.
 */
bool write_chunk(std::ostream& out, const VoxelChunk& chunk);
bool read_chunk(std::istream& in, VoxelChunk* out);

// --- Little endian scalars, for grid bodies ---

void write_u8(std::ostream& out, uint8_t v);
void write_u16(std::ostream& out, uint16_t v);
void write_u32(std::ostream& out, uint32_t v);
void write_i32(std::ostream& out, int32_t v);
void write_f32(std::ostream& out, float v);

bool read_u8(std::istream& in, uint8_t* out);
bool read_u16(std::istream& in, uint16_t* out);
bool read_u32(std::istream& in, uint32_t* out);
bool read_i32(std::istream& in, int32_t* out);
bool read_f32(std::istream& in, float* out);

}

#endif //BUSINESS_GAME_VOXELFILE_HPP
