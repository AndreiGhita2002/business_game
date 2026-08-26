//
// Created by Andrei Ghita on 26.08.2026.
//

#ifndef BUSINESS_GAME_VOXELFILE_HPP
#define BUSINESS_GAME_VOXELFILE_HPP
#include <cstdint>
#include <iosfwd>
#include <map>
#include <string>

#include "voxel/VoxelGrid.hpp"

class VoxelView;

/**
 * On disk format for voxel grids.
 *
 * A file is a readable header followed by a binary blob:
 *
 *   BGVOX 1                      <- magic and format version, always line one
 *   name: my grid
 *   description: whatever this grid is
 *   type: VoxelMap               <- picks the loader, see load_grid()
 *   voxel_bytes: 1
 *   chunk_size: 16
 *   palette_size: 12
 *   --- BINARY ---               <- everything past this line's newline is binary
 *   <common section>
 *   <grid specific body>
 *
 * Header lines are "key: value", in any order after the magic. Blank lines and
 * lines starting with '#' are ignored, unknown keys are kept in Header::fields
 * so an older reader does not lose them.
 *
 * The common section is written for every grid type, in this order:
 *   u32 palette_size, then that many { u8 id, u8 r, u8 g, u8 b, u8 a }
 *   f32 x10: translation xyz, rotation xyzw, scale xyz
 *
 * The body after it is up to each grid (VoxelGrid::write_body), but the voxels
 * inside it always go through write_chunk()/read_chunk(), so every grid stores
 * voxels the same way no matter how it lays out the rest.
 *
 * All integers are little endian, floats are IEEE 754 bit patterns written as
 * u32, so a file written on one machine reads on another.
 */
namespace voxel_file {

// First line of every file: MAGIC, a space, then the format version.
inline constexpr char MAGIC[] = "BGVOX";
inline constexpr int FORMAT_VERSION = 1;
// The line that closes the readable header.
inline constexpr char BINARY_MARKER[] = "--- BINARY ---";
// The extension these files are expected to carry.
inline constexpr char FILE_EXTENSION[] = ".bgvox";

/**
 * The readable part of a file. Cheap to read on its own, so a file browser can
 * list a directory without touching any voxel data.
 */
struct Header {
    int version = FORMAT_VERSION;
    std::string name;
    std::string description;
    std::string grid_type;
    // Every "key: value" line, including the ones mirrored in the members
    // above. Grid specific loaders read their own keys from here.
    std::map<std::string, std::string> fields;

    /** The value of a header key, or nullptr when the file did not carry it. */
    const std::string* find(const std::string& key) const;
    /** The value of a header key as an int, or fallback when missing or unparsable. */
    int get_int(const std::string& key, int fallback) const;
};

/**
 * Everything a grid specific loader is handed once the parts of the file that
 * are common to all grids have been read.
 */
struct LoadContext {
    VoxelView* view;
    const Header* header;
    // The palette read from the file, or the shared one the caller asked for.
    VoxelColourMap palette;
    // Applied to the grid by load_grid() after the loader returns, so a loader
    // must not build any models: VoxelMap::set_transform folds the transform
    // into the models it already has.
    Transform transform;
};

/**
 * Builds a grid from the body of a file. The stream is positioned at the first
 * byte after the common section. Returns nullptr on a malformed body, and
 * ownership of the grid passes to the caller otherwise.
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
 * The name and description are stored on the grid as well, so a later save
 * without them keeps what was used here.
 * Returns false and logs on any failure.
 */
bool save_grid(VoxelGrid* grid, const std::string& path,
               const std::string& name, const std::string& description);

/** Saves using the name and description the grid already carries. */
bool save_grid(VoxelGrid* grid, const std::string& path);

/** Reads only the readable header of a file. */
bool read_header(const std::string& path, Header* out);

/**
 * Reads the header, then hands the rest of the file to the loader registered
 * for the grid type named in it.
 *
 * @param path: the file to read.
 * @param view: the view the new grid is drawn by.
 * @param shared_palette: when set, the new grid uses this palette and the one
 *        in the file is dropped. Pass the scene's palette to keep every grid
 *        on the same colours, or nullptr to use the file's own.
 * @param out_header: filled in with the header when not null.
 * @return the new grid, owned by the caller, or nullptr on any failure.
 */
VoxelGrid* load_grid(const std::string& path, VoxelView* view,
                     VoxelColourMap shared_palette = nullptr,
                     Header* out_header = nullptr);

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
