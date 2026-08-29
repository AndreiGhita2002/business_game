//
// Created by Andrei Ghita on 29.08.2026.
//
// The .bgvox format in voxel/VoxelFile.cpp: the chunk encoding, the little
// endian scalars, and saving and loading whole grid trees.
//
// The grids are built with a null VoxelView and never meshed, so none of this
// needs a window.
//

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

// First, so that raylib.h is in before raymath.h
#include "tests/TestHelpers.hpp"

#include "voxel/VoxelFile.hpp"

namespace {

constexpr size_t CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
// One byte of encoding and four of length in front of every chunk.
constexpr size_t CHUNK_OVERHEAD = 5;

Quaternion yaw(const float degrees) {
    return QuaternionFromAxisAngle(Vector3{0.0f, 1.0f, 0.0f}, degrees * DEG2RAD);
}

/** The bytes write_chunk() produces for a chunk. */
std::string encode(const VoxelChunk& chunk) {
    std::ostringstream out(std::ios::binary);
    REQUIRE(voxel_file::write_chunk(out, chunk));
    return out.str();
}

/** A chunk through write_chunk() and back out of read_chunk(). */
VoxelChunk round_trip(const VoxelChunk& chunk) {
    std::istringstream in(encode(chunk), std::ios::binary);
    VoxelChunk out{};
    REQUIRE(voxel_file::read_chunk(in, &out));
    return out;
}

/** The first line of a file, so the magic can be read back. */
std::string first_line(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::string line;
    std::getline(in, line);
    return line;
}

}

// --- The chunk encoding ---

TEST_CASE("a chunk of air survives the round trip", "[voxelfile][chunk]") {
    const VoxelChunk chunk{};
    REQUIRE(round_trip(chunk) == chunk);
}

TEST_CASE("a chunk with voxels in it survives the round trip", "[voxelfile][chunk]") {
    VoxelChunk chunk{};

    SECTION("a single voxel") {
        chunk[0] = 1;
        REQUIRE(round_trip(chunk) == chunk);
    }

    SECTION("the last voxel, so the final run reaches the end") {
        chunk[CHUNK_VOLUME - 1] = 2;
        REQUIRE(round_trip(chunk) == chunk);
    }

    SECTION("one half solid, which is two long runs") {
        for (size_t i = 0; i < CHUNK_VOLUME / 2; ++i) chunk[i] = 3;
        REQUIRE(round_trip(chunk) == chunk);
    }

    SECTION("every voxel different from its neighbour, the worst case") {
        for (size_t i = 0; i < CHUNK_VOLUME; ++i) chunk[i] = i % 2 ? 1 : 0;
        REQUIRE(round_trip(chunk) == chunk);
    }

    SECTION("a spread of ids") {
        for (size_t i = 0; i < CHUNK_VOLUME; ++i) chunk[i] = static_cast<VoxelID>(i % 251);
        REQUIRE(round_trip(chunk) == chunk);
    }
}

TEST_CASE("a chunk is written whichever way is smaller", "[voxelfile][chunk]") {
    SECTION("long runs are worth encoding") {
        const VoxelChunk air{};
        // One run of the whole chunk: three bytes of payload, not four thousand
        REQUIRE(encode(air).size() == CHUNK_OVERHEAD + 3);
    }

    SECTION("a noisy chunk falls back to the raw bytes") {
        VoxelChunk noisy{};
        for (size_t i = 0; i < CHUNK_VOLUME; ++i) noisy[i] = i % 2 ? 1 : 0;
        REQUIRE(encode(noisy).size() == CHUNK_OVERHEAD + CHUNK_VOLUME);
    }
}

TEST_CASE("a chunk carries its length, so a reader can skip it", "[voxelfile][chunk]") {
    VoxelChunk chunk{};
    chunk[7] = 1;

    // Two chunks back to back, with the second one the reader actually wants
    std::ostringstream out(std::ios::binary);
    REQUIRE(voxel_file::write_chunk(out, chunk));
    VoxelChunk wanted{};
    wanted[9] = 2;
    REQUIRE(voxel_file::write_chunk(out, wanted));

    std::istringstream in(out.str(), std::ios::binary);

    // Skipping the first one using only its own header
    uint8_t encoding = 0;
    uint32_t payload_bytes = 0;
    REQUIRE(voxel_file::read_u8(in, &encoding));
    REQUIRE(voxel_file::read_u32(in, &payload_bytes));
    in.seekg(payload_bytes, std::ios::cur);

    VoxelChunk read_back{};
    REQUIRE(voxel_file::read_chunk(in, &read_back));
    REQUIRE(read_back == wanted);
}

TEST_CASE("read_chunk turns away a chunk it cannot trust", "[voxelfile][chunk]") {
    VoxelChunk out{};

    SECTION("an encoding it does not know") {
        std::ostringstream bytes(std::ios::binary);
        voxel_file::write_u8(bytes, 42);
        voxel_file::write_u32(bytes, 0);
        std::istringstream in(bytes.str(), std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, &out));
    }

    SECTION("a run length payload that is not whole runs") {
        std::ostringstream bytes(std::ios::binary);
        voxel_file::write_u8(bytes, 1);
        voxel_file::write_u32(bytes, 4);  // not a multiple of three
        voxel_file::write_u32(bytes, 0);
        std::istringstream in(bytes.str(), std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, &out));
    }

    SECTION("runs that do not cover the whole chunk") {
        std::ostringstream bytes(std::ios::binary);
        voxel_file::write_u8(bytes, 1);
        voxel_file::write_u32(bytes, 3);
        voxel_file::write_u16(bytes, 10);  // ten voxels, not four thousand
        voxel_file::write_u8(bytes, 1);
        std::istringstream in(bytes.str(), std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, &out));
    }

    SECTION("a raw chunk that claims the wrong length") {
        std::ostringstream bytes(std::ios::binary);
        voxel_file::write_u8(bytes, 0);
        voxel_file::write_u32(bytes, 12);
        std::istringstream in(bytes.str(), std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, &out));
    }

    SECTION("a stream that ends part way through") {
        std::istringstream in("", std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, &out));
    }

    SECTION("nowhere to put the result") {
        std::istringstream in(encode(VoxelChunk{}), std::ios::binary);
        REQUIRE_FALSE(voxel_file::read_chunk(in, nullptr));
    }
}

// --- The scalars ---

TEST_CASE("the scalars come back as they went in", "[voxelfile][scalars]") {
    std::ostringstream out(std::ios::binary);
    voxel_file::write_u8(out, 0);
    voxel_file::write_u8(out, 255);
    voxel_file::write_u16(out, 0xBEEF);
    voxel_file::write_u32(out, 0xDEADBEEF);
    voxel_file::write_i32(out, -123456);
    voxel_file::write_i32(out, 2147483647);
    voxel_file::write_f32(out, -3.5f);
    voxel_file::write_f32(out, 0.1f);

    std::istringstream in(out.str(), std::ios::binary);
    uint8_t u8_low = 1, u8_high = 0;
    uint16_t u16 = 0;
    uint32_t u32 = 0;
    int32_t negative = 0, positive = 0;
    float f_negative = 0.0f, f_fraction = 0.0f;

    REQUIRE(voxel_file::read_u8(in, &u8_low));
    REQUIRE(voxel_file::read_u8(in, &u8_high));
    REQUIRE(voxel_file::read_u16(in, &u16));
    REQUIRE(voxel_file::read_u32(in, &u32));
    REQUIRE(voxel_file::read_i32(in, &negative));
    REQUIRE(voxel_file::read_i32(in, &positive));
    REQUIRE(voxel_file::read_f32(in, &f_negative));
    REQUIRE(voxel_file::read_f32(in, &f_fraction));

    REQUIRE(u8_low == 0);
    REQUIRE(u8_high == 255);
    REQUIRE(u16 == 0xBEEF);
    REQUIRE(u32 == 0xDEADBEEF);
    REQUIRE(negative == -123456);
    REQUIRE(positive == 2147483647);
    // Floats go out as their bit pattern, so they come back exactly
    REQUIRE(f_negative == -3.5f);
    REQUIRE(f_fraction == 0.1f);
}

TEST_CASE("the scalars are little endian, whatever the machine is", "[voxelfile][scalars]") {
    std::ostringstream out(std::ios::binary);
    voxel_file::write_u32(out, 0x01020304);
    voxel_file::write_u16(out, 0x0102);

    const std::string bytes = out.str();
    REQUIRE(bytes.size() == 6);
    REQUIRE(static_cast<uint8_t>(bytes[0]) == 0x04);
    REQUIRE(static_cast<uint8_t>(bytes[1]) == 0x03);
    REQUIRE(static_cast<uint8_t>(bytes[2]) == 0x02);
    REQUIRE(static_cast<uint8_t>(bytes[3]) == 0x01);
    REQUIRE(static_cast<uint8_t>(bytes[4]) == 0x02);
    REQUIRE(static_cast<uint8_t>(bytes[5]) == 0x01);
}

TEST_CASE("reading past the end of a stream fails rather than inventing a value",
          "[voxelfile][scalars]") {
    std::istringstream in("ab", std::ios::binary);
    uint32_t value = 0;
    REQUIRE_FALSE(voxel_file::read_u32(in, &value));
}

// --- Whole files ---

TEST_CASE("a file starts with the magic and the version", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("magic" + std::string(voxel_file::FILE_EXTENSION));

    const auto palette = test::make_palette();
    const auto grid = test::make_chunk_grid(palette);
    REQUIRE(voxel_file::save_grid(grid.get(), path, "a grid", ""));

    REQUIRE(first_line(path) == std::string(voxel_file::MAGIC) + " "
                                + std::to_string(voxel_file::FORMAT_VERSION));
}

TEST_CASE("a single chunk grid survives a save and a load", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("crate.bgvox");
    const auto palette = test::make_palette();

    const Transform placed{Vector3{3.0f, -2.5f, 8.0f}, yaw(35.0f), Vector3{2.0f, 2.0f, 2.0f}};
    {
        const auto grid = test::make_chunk_grid(palette, Int3{1, 2, 3});
        REQUIRE(grid->set_voxel(Int3{4, 5, 6}, 2));
        REQUIRE(grid->set_voxel(Int3{15, 15, 15}, 3));
        grid->set_transform(placed);
        REQUIRE(voxel_file::save_grid(grid.get(), path, "crate", "a wooden box"));
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);

    REQUIRE(loaded->get_grid_type() == SINGLE_CHUNK_GRID_STR);
    REQUIRE(loaded->name == "crate");
    REQUIRE(loaded->description == "a wooden box");
    REQUIRE_TRANSFORM_EQ(loaded->get_transform(), placed);

    REQUIRE(*loaded->get_voxel(Int3{1, 2, 3}) == 1);
    REQUIRE(*loaded->get_voxel(Int3{4, 5, 6}) == 2);
    REQUIRE(*loaded->get_voxel(Int3{15, 15, 15}) == 3);
    REQUIRE(*loaded->get_voxel(Int3{0, 0, 0}) == 0);

    // The palette in the file came back with it
    REQUIRE(loaded->voxel_colours != nullptr);
    REQUIRE(loaded->voxel_colours->size() == palette->size());

    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a name with characters that would break the header survives", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("odd.bgvox");
    const auto palette = test::make_palette();

    // A newline and a colon would both otherwise be read as header structure
    const std::string name = "a: name\nwith lines";
    {
        const auto grid = test::make_chunk_grid(palette);
        REQUIRE(voxel_file::save_grid(grid.get(), path, name, "and: a description"));
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->name == name);
    REQUIRE(loaded->description == "and: a description");
    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a voxel map survives a save and a load", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("world.bgvox");

    {
        // Two chunks across, one deep, left as air so nothing depends on the
        // noise generator staying the same
        VoxelMap map(nullptr, 32, 16, false);
        REQUIRE(map.set_voxel(Int3{0, 0, 0}, 1));
        REQUIRE(map.set_voxel(Int3{20, 3, 5}, 2));
        REQUIRE(map.set_voxel(Int3{31, 15, 15}, 3));
        REQUIRE(voxel_file::save_grid(&map, path, "world", "the map"));
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->get_grid_type() == VOXEL_MAP_STR);

    const Int2 size = loaded->get_size();
    REQUIRE(size.x == 32);
    REQUIRE(size.y == 16);

    REQUIRE(*loaded->get_voxel(Int3{0, 0, 0}) == 1);
    REQUIRE(*loaded->get_voxel(Int3{20, 3, 5}) == 2);
    REQUIRE(*loaded->get_voxel(Int3{31, 15, 15}) == 3);
    REQUIRE(*loaded->get_voxel(Int3{10, 10, 10}) == 0);

    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a whole tree comes back in the shape it was saved in", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("tree.bgvox");
    const auto palette = test::make_palette();

    const Transform child_local{Vector3{1.0f, 2.0f, 3.0f}, yaw(20.0f), Vector3{1.0f, 1.0f, 1.0f}};
    const Transform grandchild_local{Vector3{0.0f, 4.0f, 0.0f}, QuaternionIdentity(),
                                     Vector3{0.5f, 0.5f, 0.5f}};
    {
        auto root = std::make_unique<SingleChunkGrid>(nullptr, palette);
        auto child = std::make_unique<SingleChunkGrid>(nullptr, test::make_palette());
        auto grandchild = std::make_unique<SingleChunkGrid>(nullptr, test::make_palette());

        root->set_voxel(Int3{0, 0, 0}, 1);
        child->set_voxel(Int3{1, 1, 1}, 2);
        grandchild->set_voxel(Int3{2, 2, 2}, 3);
        child->set_transform(child_local);
        grandchild->set_transform(grandchild_local);

        REQUIRE(root->add_child(child.get()));
        REQUIRE(child->add_child(grandchild.get()));

        REQUIRE(voxel_file::save_grid(root.get(), path, "root", ""));

        // Torn down leaves first, as the links do not own anything
        grandchild.reset();
        child.reset();
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);

    REQUIRE(loaded->get_parent() == nullptr);
    REQUIRE(loaded->get_children().size() == 1);

    VoxelGrid* child = loaded->get_children()[0];
    REQUIRE(child->get_parent() == loaded);
    REQUIRE(child->get_children().size() == 1);
    REQUIRE_TRANSFORM_EQ(child->get_transform(), child_local);
    REQUIRE(*child->get_voxel(Int3{1, 1, 1}) == 2);

    VoxelGrid* grandchild = child->get_children()[0];
    REQUIRE(grandchild->get_parent() == child);
    REQUIRE(grandchild->get_children().empty());
    // Transforms are stored local, so the grandchild is where it was inside its
    // parent wherever the root ends up
    REQUIRE_TRANSFORM_EQ(grandchild->get_transform(), grandchild_local);
    REQUIRE(*grandchild->get_voxel(Int3{2, 2, 2}) == 3);

    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("an attachment survives the round trip and is not snapped again",
          "[voxelfile][file][attachment]") {
    const test::TempDir dir;
    const std::string path = dir.file("car.bgvox");
    const auto palette = test::make_palette();

    // Deliberately not where snapping would put it, so that a load which
    // re-snapped the grid would move it and fail the transform check below
    const Transform off_anchor{Vector3{9.0f, 9.0f, 9.0f}, yaw(45.0f), Vector3{1.0f, 1.0f, 1.0f}};
    {
        auto anchor = std::make_unique<SingleChunkGrid>(nullptr, palette);
        auto wheel = std::make_unique<SingleChunkGrid>(nullptr, palette);
        anchor->set_voxel(Int3{4, 5, 6}, 1);
        wheel->set_voxel(Int3{1, 1, 1}, 2);
        wheel->set_transform(off_anchor);

        REQUIRE(wheel->attach_to(anchor.get(), Int3{4, 5, 6}, Int3{1, 1, 1}, false));
        REQUIRE(voxel_file::save_grid(anchor.get(), path, "car", ""));
        wheel.reset();
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->get_children().size() == 1);

    VoxelGrid* wheel = loaded->get_children()[0];
    REQUIRE(wheel->is_attached());
    REQUIRE(wheel->get_attachment()->anchor == loaded);
    REQUIRE(wheel->get_attachment()->anchor_voxel == Int3{4, 5, 6});
    REQUIRE(wheel->get_attachment()->local_voxel == Int3{1, 1, 1});
    REQUIRE_TRANSFORM_EQ(wheel->get_transform(), off_anchor);

    // Still a real attachment, so the connectors are still protected
    REQUIRE_FALSE(wheel->set_voxel(Int3{1, 1, 1}, 0));
    REQUIRE_FALSE(loaded->set_voxel(Int3{4, 5, 6}, 0));

    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a grid merely hanging off its parent comes back unattached",
          "[voxelfile][file][attachment]") {
    const test::TempDir dir;
    const std::string path = dir.file("carried.bgvox");
    const auto palette = test::make_palette();

    {
        auto root = std::make_unique<SingleChunkGrid>(nullptr, palette);
        auto child = std::make_unique<SingleChunkGrid>(nullptr, palette);
        root->set_voxel(Int3{0, 0, 0}, 1);
        child->set_voxel(Int3{1, 1, 1}, 2);
        REQUIRE(root->add_child(child.get()));
        REQUIRE(voxel_file::save_grid(root.get(), path, "carried", ""));
        child.reset();
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->get_children().size() == 1);
    REQUIRE_FALSE(loaded->get_children()[0]->is_attached());
    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a child sharing its parent's colours comes back on the same map",
          "[voxelfile][file][palette]") {
    const test::TempDir dir;
    const std::string path = dir.file("shared.bgvox");
    const auto palette = test::make_palette();

    {
        // Both grids on one colour map, which is what the editor does
        auto root = std::make_unique<SingleChunkGrid>(nullptr, palette);
        auto child = std::make_unique<SingleChunkGrid>(nullptr, palette);
        root->set_voxel(Int3{0, 0, 0}, 1);
        child->set_voxel(Int3{1, 1, 1}, 2);
        REQUIRE(root->add_child(child.get()));
        REQUIRE(voxel_file::save_grid(root.get(), path, "shared", ""));
        child.reset();
    }

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    REQUIRE(loaded->get_children().size() == 1);

    // One map between them, not two equal copies
    REQUIRE(loaded->voxel_colours == loaded->get_children()[0]->voxel_colours);
    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("a shared palette puts the whole tree on the scene's colours",
          "[voxelfile][file][palette]") {
    const test::TempDir dir;
    const std::string path = dir.file("recoloured.bgvox");

    {
        auto root = std::make_unique<SingleChunkGrid>(nullptr, test::make_palette());
        auto child = std::make_unique<SingleChunkGrid>(nullptr, test::make_palette());
        root->set_voxel(Int3{0, 0, 0}, 1);
        child->set_voxel(Int3{1, 1, 1}, 2);
        REQUIRE(root->add_child(child.get()));
        REQUIRE(voxel_file::save_grid(root.get(), path, "recoloured", ""));
        child.reset();
    }

    const auto scene_palette = test::make_palette();
    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr, scene_palette);
    REQUIRE(loaded != nullptr);

    std::vector<VoxelGrid*> all;
    voxel_file::collect_grids(loaded, &all);
    REQUIRE(all.size() == 2);
    for (const VoxelGrid* grid : all) {
        REQUIRE(grid->voxel_colours == scene_palette);
    }

    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("collect_grids walks parents before children", "[voxelfile][file]") {
    const auto palette = test::make_palette();
    const auto root = test::make_chunk_grid(palette);
    const auto first = test::make_chunk_grid(palette);
    const auto second = test::make_chunk_grid(palette);
    const auto leaf = test::make_chunk_grid(palette);

    REQUIRE(root->add_child(first.get()));
    REQUIRE(root->add_child(second.get()));
    REQUIRE(first->add_child(leaf.get()));

    std::vector<VoxelGrid*> all;
    voxel_file::collect_grids(root.get(), &all);

    REQUIRE(all.size() == 4);
    REQUIRE(all[0] == root.get());
    // Every grid appears after whatever it hangs off, which is what makes the
    // ids in a file safe to resolve in one pass
    for (size_t i = 0; i < all.size(); ++i) {
        const VoxelGrid* parent = all[i]->get_parent();
        if (parent == nullptr) continue;
        const auto found = std::find(all.begin(), all.end(), parent);
        REQUIRE(found != all.end());
        REQUIRE(static_cast<size_t>(found - all.begin()) < i);
    }
}

TEST_CASE("read_header reads the readable part without the voxels", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("listing.bgvox");
    const auto palette = test::make_palette();

    {
        auto root = std::make_unique<SingleChunkGrid>(nullptr, palette);
        auto child = std::make_unique<SingleChunkGrid>(nullptr, palette);
        root->set_voxel(Int3{0, 0, 0}, 1);
        child->set_voxel(Int3{1, 1, 1}, 2);
        REQUIRE(root->add_child(child.get()));
        REQUIRE(voxel_file::save_grid(root.get(), path, "listing", "two grids"));
        child.reset();
    }

    voxel_file::Header header;
    REQUIRE(voxel_file::read_header(path, &header));

    REQUIRE(header.version == voxel_file::FORMAT_VERSION);
    REQUIRE(header.grids.size() == 2);
    // The root's details are lifted out, for listing a directory of files
    REQUIRE(header.name == "listing");
    REQUIRE(header.description == "two grids");
    REQUIRE(header.grid_type == SINGLE_CHUNK_GRID_STR);
    REQUIRE(header.get_int("chunk_size", 0) == CHUNK_SIZE);
    REQUIRE(header.get_int("grid_count", 0) == 2);

    const voxel_file::GridHeader* root_header = header.root();
    REQUIRE(root_header != nullptr);
    REQUIRE_FALSE(root_header->has_parent);
    REQUIRE(root_header->children.size() == 1);

    const voxel_file::GridHeader* child_header =
        header.find_grid(root_header->children[0]);
    REQUIRE(child_header != nullptr);
    REQUIRE(child_header->has_parent);
    REQUIRE(child_header->parent_id == root_header->id);
    REQUIRE(child_header->palette_from_parent);
}

TEST_CASE("both grid types are known to the loader", "[voxelfile][file]") {
    REQUIRE(voxel_file::can_load_grid_type(SINGLE_CHUNK_GRID_STR));
    REQUIRE(voxel_file::can_load_grid_type(VOXEL_MAP_STR));
    REQUIRE_FALSE(voxel_file::can_load_grid_type("NotAGridType"));
}

TEST_CASE("a file that cannot be read fails cleanly", "[voxelfile][file]") {
    const test::TempDir dir;

    SECTION("there is no such file") {
        REQUIRE(voxel_file::load_grid(dir.file("missing.bgvox"), nullptr) == nullptr);

        voxel_file::Header header;
        REQUIRE_FALSE(voxel_file::read_header(dir.file("missing.bgvox"), &header));
    }

    SECTION("the file is not a bgvox file at all") {
        const std::string path = dir.file("notes.txt");
        std::ofstream(path, std::ios::binary) << "just some text\n";
        REQUIRE(voxel_file::load_grid(path, nullptr) == nullptr);
    }

    SECTION("the header is right but the bodies are missing") {
        const std::string path = dir.file("truncated.bgvox");
        std::ofstream out(path, std::ios::binary);
        out << voxel_file::MAGIC << ' ' << voxel_file::FORMAT_VERSION << '\n';
        out << "grid_count: 1\n\n";
        out << voxel_file::GRID_MARKER << '\n';
        out << "id: 0\ntype: " << SINGLE_CHUNK_GRID_STR << '\n';
        out << "name: nothing\ndescription:\nparent: none\nchildren:\n";
        out << "palette_source: own\npalette_size: 0\n\n";
        out << voxel_file::BINARY_MARKER << '\n';
        out.close();

        REQUIRE(voxel_file::load_grid(path, nullptr) == nullptr);
    }

    SECTION("saving a null grid is refused") {
        REQUIRE_FALSE(voxel_file::save_grid(nullptr, dir.file("null.bgvox"), "", ""));
    }
}

TEST_CASE("saving creates the directories on the way to the file", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("a/b/c/deep.bgvox");
    const auto palette = test::make_palette();
    const auto grid = test::make_chunk_grid(palette);

    REQUIRE(voxel_file::save_grid(grid.get(), path, "deep", ""));
    REQUIRE(std::filesystem::exists(path));

    VoxelGrid* loaded = voxel_file::load_grid(path, nullptr);
    REQUIRE(loaded != nullptr);
    voxel_file::delete_grid_tree(loaded);
}

TEST_CASE("saving without a name keeps the one the grid already carries", "[voxelfile][file]") {
    const test::TempDir dir;
    const std::string path = dir.file("remembered.bgvox");
    const auto palette = test::make_palette();

    {
        const auto grid = test::make_chunk_grid(palette);
        REQUIRE(voxel_file::save_grid(grid.get(), path, "remembered", "kept"));
        // The name was written onto the grid, so the shorter overload has it
        REQUIRE(grid->name == "remembered");
        REQUIRE(voxel_file::save_grid(grid.get(), path));
    }

    voxel_file::Header header;
    REQUIRE(voxel_file::read_header(path, &header));
    REQUIRE(header.name == "remembered");
    REQUIRE(header.description == "kept");
}
