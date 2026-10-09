# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Business Game is a work-in-progress tycoon game inspired by OpenTTD, built with a custom voxel-based 3D rendering engine in C++20.

## Build Commands

There is a `Makefile` at the root wrapping CMake, so the usual things are one
word. It only drives CMake; the real makefiles are the generated ones in `build/`.

```bash
make            # configure, build and run the game
make tests      # build and run every unit test
make build      # build the game without running it
make clean      # delete the build directory
make help       # the rest

make tests CTESTFLAGS="-R attach"   # only the tests whose name matches
```

`make run` starts the game from inside `build/`, not from `build/bin/`. That is
deliberate: `main.cpp` loads its shaders from `"../resources/shaders/..."`, which
only resolves to the repository's `resources/` when the working directory is one
level below the repository root.

The same thing by hand:

```bash
# Build the project
mkdir build && cd build
cmake ..
cmake --build .

# Executable location
build/bin/business_game

# Run the unit tests
ctest --test-dir build --output-on-failure
# or the binary directly, which takes Catch2's own flags:
build/bin/business_game_tests "[voxelfile]"
```

The project uses CMake with FetchContent for dependencies (raylib, raylib-cpp,
raygui, Catch2).

There are five targets. `business_game_sim` is the simulation (`src/sim`) and
links nothing but the standard library. `business_game_lib` holds everything
else except `main()` and links the simulation, `business_game` is `main.cpp`
linked against it, and two Catch2 runners test them: `business_game_tests`
against the library, `business_game_sim_tests` against the simulation alone.
**A new source file goes in a library's list, not the executable's** - the
executable is only `main.cpp` - and in `business_game_sim` only if it is
simulation code with no raylib in it.

Pass `-DBUSINESS_GAME_BUILD_TESTS=OFF` to skip building Catch2, which is off
automatically for a web build.

## Tests

Catch2 v3 (`src/tests/`), one file per area. They run without a window, which is
what limits what they can cover.

The simulation's tests are in `src/tests/sim/` and build into
`business_game_sim_tests`, which links the simulation library and Catch2 and
nothing else - that it links at all is part of the proof that the simulation
needs nothing from the game side:

- `test_sim_core.cpp` - `Fixed`, `Pool` and its handles, `ByteWriter` /
  `ByteReader`, and `Rng` (pinned to the reference PCG32 output).
- `test_sim_routes.cpp` - what makes a route, and where a distance lands on one.
- `test_sim_simulation.cpp` - `step()`: commands in, vehicles moving, events and
  refusals out, and the (player, sequence) order.
- `test_sim_save.cpp` - a saved game loading with the same checksum and then
  ticking on in step with the original, queued commands running after a load,
  identical bytes for identical games, every malformed file refused with a
  reason, files round tripping, and `Pool::read` refusing a bad free list.
- `test_sim_determinism.cpp` - the same game twice, a recorded log replayed, and
  the log written to bytes and replayed, all agreeing on the checksum every
  tick; and the checksum noticing a different seed, a different speed, and
  different slot bookkeeping. Played on a small world of ocean, as an island
  is a megabyte to hash every tick.
- `test_sim_terrain.cpp` - the fixed point Perlin noise, the block grid and its
  height queries, ocean cells (a sea floor until written to), the default
  world (ocean round one island in the middle), world sizes and the limit,
  stone under the water, the terrain round tripping and damaged bytes refused,
  and the terrain in the checksum and the `TERR` save section.
- `test_sim_island.cpp` - footprints and their turns, specs drawn from a seed,
  centring, placement refused off the world or over land, the same island
  wherever it is put, the coast meeting the ocean at the sea floor, relief by
  elevation with the slope limit, and each biome's top blocks.

A CTest entry, `sim_uses_no_floating_point` (`cmake/CheckSimNoFloats.cmake`),
fails if the word `float` or `double` appears anywhere under `src/sim`, comments
included, so the simulation's comments say "fractional" instead.

The game side:

- `test_sim_convert.cpp` - `entity/SimConvert`: simulation to world axes, the
  heading rotation, and interpolation between two ticks.
- `test_entity_manager.cpp` - entities realised and unrealised as vehicles come
  into and out of range (against a fake `GridSink`), placed where the
  simulation says, rebuilt after `clear()`, the wheels turning on their axles,
  presenting never changing the simulation's checksum, `largest_extent()` of
  the placeholder car, and a vehicle scaled to fit a block.
- `test_transform.cpp` - the maths in `game/Transform.hpp`.
- `test_attachment.cpp` - the grid hierarchy and the attachment system.
- `test_voxel_file.cpp` - the `.bgvox` chunk encoding, scalars and whole files.
- `test_voxel_mesh.cpp` - `build_chunk_mesh_data()`: which faces come out, the
  faces dropped against a neighbouring chunk, and the baked ambient occlusion.
- `test_voxel_ray.cpp` - `voxel_ray_blocked()`, the voxel walk a shadow ray
  does, which is the testable twin of the one in `lighting.fs`, plus the boxes
  that decide which grids a draw call is traced against.
- `test_frustum.cpp` - `game/Frustum`: boxes kept and thrown away against a
  camera built the way `BeginMode3D()` builds one.
- `test_water.cpp` - how the water is cut into chunks, where its surface sits,
  which chunks a camera over one corner keeps, the chunks following the
  terrain's transform, and the waves through `water_wave_height()`, the twin
  of the vertex shader's. The `WaterView` itself needs
  a GL context and is never built.
- `test_terrain_voxels.cpp` - `entity/TerrainVoxels`: each block type's voxels
  and its colour in the palette, which blocks get lowered edges and trims,
  what each does to the voxels (grass following a lowered edge, a trim
  stopping over a lowered floor edge), a terrain drawn into a `VoxelMap`, only
  its land cells making chunks, the sparse map's chunks and remeshing, and
  `sim::PerlinNoise` checked against
  `siv::PerlinNoise` (same permutation, values within 1e-3), which only this
  side may include.
- `TestHelpers.hpp` - a palette, a self-deleting temp directory, `voxel_at()`
  (a voxel read with a missing map chunk as air), and the
  `REQUIRE_VEC3_EQ` / `REQUIRE_QUAT_EQ` / `REQUIRE_TRANSFORM_EQ` comparisons.
  Include it **first** in a test file: `raymath.h` redefines raylib's vector
  types unless `raylib.h` is in ahead of it, and this header gets that order
  right.

Grids in tests are built with a null `VoxelView`. That is only safe because
`update_models()` is the one thing that dereferences it, and it is also the only
call that needs an OpenGL context, so **tests must never call `update_models()`**.
Anything that meshes, draws or reads input is out of reach and is not covered:
`VoxelMesher`, `VoxelView`, `WaterView`, `Light`, and all of `src/ui`.

## Architecture

### Simulation and Presentation

The game is split in two. The **simulation** (`src/sim`) is the single source
of truth for gameplay. The **presentation** (everything else) reads it and
draws it. Voxel grids are purely cosmetic: the simulation never reads one. This
is what lets the simulation run things nobody is looking at, things with no
visual counterpart at all, and later run in lockstep across machines.

Rules the simulation keeps, and that anything added to it must keep:

- **No raylib, enforced.** `business_game_sim` has no raylib on its include
  path, so including a `voxel/` or `ui/` header from `src/sim` fails to compile.
- **No hardware fractional maths.** Everything fractional is `sim::Fixed`
  (48.16 in an `int64_t`, `sim::BLOCK_SIZE` (4) units to a terrain block, so
  one unit per voxel on screen). A native and a web build
  have to agree bit for bit. CTest checks for the keywords.
- **Fixed ticks.** `sim::TICKS_PER_SECOND` is 20. The simulation counts ticks,
  never seconds or frames, and knows nothing about the camera.
- **Deterministic iteration.** Objects live in `sim::Pool` (slots plus a free
  list, walked in slot order) and are named by generational `sim::Handle`s,
  never pointers. Never iterate simulation state through an unordered
  container. Randomness comes from the seeded `sim::Rng` (PCG32, hand written
  because the std distributions differ between standard libraries).
- **Every change is a command.** `sim::Command` is a class per action
  (`AddRoute`, `SpawnVehicle`, `DespawnVehicle`, `SetVehicleSpeed`,
  `SetWaterLevel`) with
  `apply(World&)`, `write_payload()` and `clone()`. A new one also needs a
  `CommandType` number (the wire format, so never reused) and a case in
  `read_payload()` in `Command.cpp`. `World` is the mutable state and only a
  command inside `Simulation::step()` ever holds one; everyone else gets the
  const accessors.
- `CommandQueue` stamps a sequence number on submit and the tick on `take()`,
  so locally a command runs at the next tick. Lockstep would stamp the tick at
  submit time, a few ticks ahead, and send it to everyone.
- `step()` applies a tick's commands sorted by (player, sequence), refusing
  bad ones with a `CommandRejected` event, then advances the systems. Events
  (`RouteAdded`, `VehicleSpawned`, `VehicleDespawned`) are for discrete changes
  and last one step; continuous state is read, not pushed.
- `Simulation::checksum()` is FNV-1a over `write_state()`, which writes the
  whole state including the pools' slot bookkeeping. Anything left out of
  `write_state()` is invisible to desync checks.

**Saved games** (`sim/Save.cpp/hpp`) hold the simulation and the commands still
queued, and nothing from the presentation: everything on screen is rebuilt from
the simulation after a load.

- A 12 byte header (`BGSV`, `SAVE_FORMAT_VERSION`, section count), then one
  section per system: `u32 tag, u32 version, u32 payload_bytes, payload`. The
  sections are `CORE` (tick, Rng and water level, version 2), `TERR` (the
  cells, a land cell's blocks raw, version 2), `ROUT`, `VEHI` and `CMDS` (the
  `CommandQueue`: waiting commands and the next sequence number, so they run on
  the first tick after the load).
- **No migration.** A section of another version, an unknown section, a
  missing or repeated one, or a payload not read to its last byte refuses the
  whole load with a message saying which. Bump a section's `*_VERSION` constant
  whenever its layout changes; older saves of it then stop loading.
- The sections are written with the same writers `write_state()` uses, and the
  pools save their slot bookkeeping (`Pool::write` / `Pool::read`), so handles
  stay valid across a load and a loaded game's checksum equals the saved one.
  A new system needs a writer, a reader, a section tag and version, and an
  entry in both `write_save()` and `read_save()`.
- `SaveAccess` in `Save.cpp` is the only thing outside a command that writes to
  a `World`. Routes are rebuilt through `Routes::build()` on load rather than
  trusted, and a damaged free list is refused.
- `save_to_file()` writes beside the target and renames over it, so a failed
  save leaves the previous one standing.
- In the game, F5 saves to `saves/quicksave.bgsave` (git ignored) and F9 loads
  it, both handled at the start of `mainLoop()`, between ticks. A load is
  assigned into the existing `global::simulation` and `global::commands`, as
  the vehicle panel points at both, after `EntityManager::clear()`, and
  `start_world()` fits the map, water and camera to the loaded terrain. A failed
  load leaves the game as it was. The readout shows how either went. A save
  menu is a `TODO (ui)` next to `QUICKSAVE_PATH`.

**Terrain** (`sim/Terrain.cpp/hpp`, `sim/Island.cpp/hpp`, `sim/Noise.cpp/hpp`)
is the simulation's: a 3D grid of blocks (`sim::BlockType`: air, stone, dirt,
grass, and for the islands' biomes sand, sandstone, snow and gravel), x and y
across, z up, each `BLOCK_SIZE` units on a side. It is the gameplay grid. See
`docs/terrain.md` for the design it follows.

- **Cells.** The world is `cells_x` by `cells_y` cells, each `CELL_BLOCKS`
  (64) blocks across, and the world is 64 blocks tall: 256 voxels a cell.
  `TerrainSettings::cells_x/y` set the size, `DEFAULT_WORLD_CELLS` (10) by
  default and at most `MAX_WORLD_CELLS` (32) a side, which is set by what the
  presentation walks every frame rather than by memory.
- **Ocean cells take no memory.** A cell nothing has written to is ocean,
  `SEA_FLOOR_BLOCKS` (2) of stone under every column and air above, and holds
  no blocks. Writing anything else into one gives it blocks of its own, the
  sea floor copied in first (`set()`); `reset_cell()` makes it ocean again.
  Every query answers the same either way. `Terrain::of_blocks(x, y, z)` is a
  small terrain of air, each column its own cell, which the tests build by hand.
- **Water.** `DEFAULT_WATER_LEVEL` is 15, the top at 16 units, four blocks up:
  two blocks of water over the sea floor. Every block wholly under the water
  (`block_under_water()`: its top no higher than `water_level + 1`) is
  stone; `TerrainSettings::water_level` says which level, and
  `Simulation(seed, settings)` starts the water there.
  `DEFAULT_WATER_LEVEL` / `MIN_WATER_LEVEL` live in `Terrain.hpp` for that reason.
- `generate_terrain(TerrainSettings)` makes the ocean and, unless
  `centre_island` is off, one island in the middle drawn from `seed`
  (`random_island_spec()`), turned until it fits. `cells_x` of 0 makes no
  terrain at all, which is what a load starts from.
- **Islands** (`sim/Island.hpp`, its header comment has the steps). An
  `IslandSpec` is a shape (the tetrominoes: square, line, L, T, zigzag), a
  quarter turn, an elevation (flat, hilly, mountainous), a biome (grassland,
  desert, snowy) and a noise seed, and generates the same island wherever it
  is put (`place_island()`, which refuses a footprint off the world, over
  land, or on cells smaller than `CELL_BLOCKS`). In integers and `Fixed`: a
  chamfer distance to the footprint's edge, a coast `ISLAND_COAST_MARGIN` in
  from it wobbled by noise, relief by elevation (octave noise, ridged for
  mountains) ramped in from the beach, then a slope limit (a block a step, two
  in the mountains), with the columns within `ISLAND_EDGE_FLOOR` of the edge
  pinned to the sea floor so an island always meets the ocean round it. The
  biome picks the top block and what is under it; beaches are sand (gravel
  when snowy), and high grassland goes to rock and then snow.
- `sim::PerlinNoise` is a port of `siv::PerlinNoise` (`includes/`) to `Fixed`:
  the same permutation from the same seed (it shuffles with `std::mt19937` and
  a plain modulo, both exactly specified) and the same maths, so it agrees with
  the original to within rounding. `noise2d()` samples siv's plane at
  z = 0.34567, as siv's `noise2D()` does.
- `Simulation(seed, TerrainSettings)` generates it, `World::terrain` holds it,
  `Simulation::terrain()` reads it, and it is in `write_state()` and so in the
  checksum (a land cell is a quarter of a megabyte, written in one copy).
  Nothing changes it after generation yet: there is no command for it, and an
  island bought during play would want one (`PlaceIsland`).
- `column_height()` (blocks) and `ground_level()` (units, for a point) are
  what anything that sits on the ground asks.

Routes and vehicles are placeholders for testing the split: a route is a closed
loop of segments that each run along x or y (so lengths are exact without a
square root), and a vehicle drives round one at a fixed speed per tick.

**Presentation of the simulation** (`src/entity`):

- `TerrainVoxels` draws the terrain into the `VoxelMap`, each block as a cube
  of `BLOCK_VOXELS` (= `sim::BLOCK_SIZE`, 4) voxels a side: grass brown with
  the top voxel of each column green, every other block its own colour all
  through (`block_voxel()`; ids 14 to 17 in the map's palette for sand,
  sandstone, snow and gravel). The VoxelView is built at
  `terrain_voxel_size()`, and `build_terrain_voxels()` clears the map and
  writes the **land cells only**, up to the top of each column, straight into
  chunks it makes as it goes. Ocean cells are left out: the WaterView draws
  their sea floor as a plane, which is what keeps a world map's chunks down to
  its islands. A terrain bigger than the map is cut off, and reported.
- **Block detail**, purely visual, from each block's four side neighbours and
  the blocks above and below it (`block_detail()` -> `BlockDetail`, side
  masks `SIDE_X_POS` and so on). It softens the block grid without hiding it:
  - *Lowered edges*: a solid block with air above it loses its top row of
    voxels on each side with air beside it. Grass comes down with it (green is
    the top voxel of each column). A buried block keeps its edges, so a cliff
    face gets no grooves.
  - *Trims*, **off** (`TERRAIN_TRIMS` 0 in `TerrainVoxels.hpp`; Andrei did
    not like how they looked, 1 or `-DTERRAIN_TRIMS=1` brings them back and
    the tests follow the switch): an air block with a solid block under it
    gets a row of voxels on
    its bottom layer along each side with a solid block beside it, filling the
    inside corner. It is made of the floor (`trim_type`, green on grass), and
    leaves out the voxels over the floor's own lowered edges
    (`floor_lowered`) so it never hangs over a notch.
  - Any number of each on one block. Outside the terrain is air, so the map's
    border is bevelled too. Vehicles still drive on the block tops.

- `Entity` is a simulation object made visible: a grid tree it owns, plus
  cosmetic `Script`s. `on_tick()` copies what the simulation says after every
  step; `present()` places the grids every frame. `VehicleEntity` keeps the
  last two ticks' poses and draws between them, so 20 ticks a second looks
  smooth (at the cost of being up to a tick behind).
- `EntityManager` realises an entity for every vehicle within
  `realize_radius` of the camera and drops it past `unrealize_radius` (two
  radii, so the boundary does not flicker; 448 and 512 by default). A vehicle
  without an entity keeps
  driving; when the camera comes back it reappears exactly where the
  simulation has it. Nothing flows back into the simulation.
- **Vehicles are scaled to fit a block.** As it realises one, the manager
  scales the root grid evenly so that `largest_extent()` of the tree (the
  longest side of the box round every solid voxel, wheels included, in the
  root's voxels) times the scale is `vehicle_size`, `VEHICLE_SIZE` =
  `BLOCK_VOXELS` world units. The placeholder car is 8 voxels long, so it is
  drawn at half scale. The wheels are children and shrink with it, and their
  spin radius is scaled too so they still roll without slipping. A
  `vehicle_size` of 0 draws vehicles as built.
- `AssetRegistry` turns a `sim::AssetId` (a name like `"car.blue"`) into a grid
  tree, from a `.bgvox` file or a builder function. `placeholder_car_builder()`
  builds a car in code: a body plus four wheel grids attached through
  `attach_to()`, named `WHEEL_GRID_NAME`, which is how `WheelSpinScript` finds
  them to turn them at the vehicle's speed.
- `GridSink` is the seam between an entity and the VoxelView: an entity hands
  its grids in and takes them back out before deleting them. The VoxelView
  implements it, the tests fake it.
- An entity owns exactly the grids its asset produced, recorded when it was
  made. A grid someone attaches to one later is not deleted with it.
- `SimConvert.hpp` is the one-way conversion: simulation (x, y ground, z up) to
  world (X, Y up, Z), `Fixed` to `float`, and a ground direction to a heading.
  It assumes the map sits at the world origin.

**Grids are deleted now.** An entity's grids go every time it leaves range, so
anything holding a `VoxelGrid*` past a frame has to let go when told:
`VoxelView::add_grid_removal_listener()` is called with the grids just before
they are deleted. `GridTransformMenu`, `AttachMenu` (through the transform
menu) and `VoxelEditor` have `forget_grids()` for it, hooked up in `main.cpp`.

### Scene Graph (ViewNode Tree)

The UI/scene uses a `ViewNode` tree hierarchy with recursive update/render traversal:
- `root_view` → `VoxelView` (→ `VoxelEditor`) and `UIView` (→ UI elements),
  where `UIView` is a sibling of `VoxelView` and so is updated and drawn after it
- ViewNodes manage parent/child/sibling relationships
- Located in `src/game/ViewNode.hpp`
- `VoxelEditor` lives in `src/ui/`, `VoxelView` in `src/voxel/`

### Voxel System

**VoxelGrid** (abstract base in `src/voxel/VoxelGrid.hpp`) has two implementations:
- **VoxelMap** (`src/voxel/VoxelMap.cpp/hpp`) - Chunk-based storage (16x16x16 chunks keyed by `Int3`), any number of chunks tall (`get_height()`; the game's is 256 voxels, 16 chunks), the size of the whole world (2560 voxels across by default). **Sparse**: a chunk only exists once something is written to it (`ensure_chunk()`, which `write_voxel()` calls; air into a missing chunk does nothing), and `get_voxel()` is null where there is none, so read through `is_solid()` or check for null. `clear()` drops every chunk and its model, `resize()` is a new size of air. It starts as air: the terrain is the simulation's, drawn in by `entity/TerrainVoxels`. A write on a chunk's border remeshes only the neighbours that exist.
- **SingleChunkGrid** (`src/voxel/SingleChunkGrid.cpp/hpp`) - Single chunk for the voxel editor

**VoxelMesher** (`src/voxel/VoxelMesher.cpp/hpp`) converts voxel data to 3D
meshes with per-material generation.

- `build_chunk_mesh_data()` is the CPU half and needs no OpenGL context, which
  is what lets the tests cover it. `upload_chunk_mesh()` is the half that does.
  `build_chunk_mesh()` is still the two together.
- It takes a `VoxelNeighbourSampler`, which answers what sits one voxel outside
  the chunk. The chunk is copied into an 18x18x18 padded array first, so a
  lookup across a border is an ordinary array read. A face against a solid
  neighbour is left out whichever chunk that neighbour is in, and an empty
  sampler means air outside (what a SingleChunkGrid wants).
- **Per-vertex ambient occlusion** is baked into the vertex colours: each face
  corner reads the three voxels around it in the air in front of the face
  (`vertex_ao()`), and the quad is split along the darker diagonal so the shade
  does not crease the wrong way. `AO_SHADE` is the brightness of the four
  levels, so changing it means remeshing.
- The colours are a shade, not a tint. `lighting.fs` reads `fragColor.r` as the
  occlusion and no longer multiplies the vertex colour into the material.

**VoxelVolume** (`src/voxel/VoxelVolume.cpp/hpp`) - The same voxels as a 3D
texture, one byte each, for the lighting shader to trace shadow rays through.
`VoxelMap::update_volume()` uploads the chunks that have changed, tracked by
`chunk_volume_dirty` because the mesh and the volume are brought up to date by
different calls. This is the one file that calls OpenGL directly: rlgl has no 3D
textures. The texture is a **window** of the map, not all of it - a world is
far bigger than a 3D texture can be. `VoxelView::set_volume_window()` puts it
over the land (`start_world()` in `main.cpp` hands it the box round the land
cells, as tall as the land), at most `MAX_WORLD_VOLUME_SIDE` (1024) across,
whole chunks, z from 0. `world_volume_origin` is where it starts:
`update_volume()` uploads a chunk at its place less that origin and skips one
outside, and `bindWorldVolume()` puts a translation by minus the origin after
the map's inverse, so the shader and its C++ twin are unchanged. Outside the
window nothing casts a shadow. The shader is told it ends at
`VoxelView::world_volume_top`, the map's `solid_top()` (one above its highest
solid layer), refreshed whenever `update_volume()` uploads anything. Nothing
above that can block a ray, so a ray leaving the shorter box is exactly as lit,
and does not walk the empty sky of a tall map up to the ceiling.

**VoxelBrickAtlas** (`src/voxel/VoxelBrickAtlas.cpp/hpp`) - One VoxelVolume cut
into chunk-sized bricks, a slot per grid, so that every grid that is not the map
can cast a shadow without needing a texture unit of its own. 8x8x4 bricks is 256
grids in a megabyte. A grid holds its slot in `VoxelGrid::volume_slot` and
returns it when it is destroyed; `volume_dirty` says when its brick needs
writing again.

### Grid Transforms

Grids form their own tree, separate from the ViewNode tree, and compose
transforms the way any scene graph does (`src/voxel/VoxelGrid.cpp`):

- `VoxelGrid::transform` is **local**: where the grid sits relative to its
  parent, or relative to the world when it has none. `get_transform()` /
  `set_transform()` are the accessors, and saving writes this one.
- `get_world_transform()` walks up the parent chain and folds each transform in
  (`transform_transform(local, parent_world)`). Everything that draws, picks or
  measures a distance goes through it: `voxel_model_matrix()` and so both
  `VoxelView::drawVoxelModel` and `find_voxel_on_ray`, and the render distance
  checks in `update_models()`. It is recomputed per call, not cached.
- `set_parent()` / `add_child()` build the tree. The links do not own anything -
  the VoxelView still owns every grid in `voxel_grids` - and a parent that is
  already below the grid is refused, as that would make `get_world_transform()`
  recurse forever. A grid whose parent is destroyed keeps its world place.
- `ModelInfo::transform` is **local to its grid**, so a VoxelMap chunk carries
  only its chunk offset and a SingleChunkGrid model is at identity. Nothing
  bakes the grid transform into a model, which is why moving a grid never needs
  a remesh.

Three levels compose at draw time: model inside grid, grid inside its parents,
parents in the world.

### Grid Attachment

Attaching (`VoxelGrid::attach_to`) is the hierarchy above plus a pair of voxels:
the grid becomes a child of the anchor, and one voxel on each side is named as
the connector that holds them together (`Attachment` in `VoxelGrid.hpp`). This is
how a voxel vehicle is built - a wheel attached to a car moves with the car
because it is a child of it, and spins on its own because its local transform is
still its own.

- Both connectors have to be solid voxels of their grid, and an anchor already
  below the grid is refused, the same rule `set_parent()` follows.
- The grid is snapped so the two connector voxels sit in the same place, leaving
  its rotation and scale alone. Turning an attached grid rotates it about its own
  origin and so carries the connector off the anchor: call `snap_to_anchor()`
  again afterwards.
- `set_voxel()` is no longer virtual. It refuses to clear a connector voxel while
  the attachment stands, then hands the write to the grid's own `write_voxel()`.
  Painting a connector another colour is still fine. `in_bounds()` is the bounds
  check each grid implements, and `is_solid()` is built on it - `VoxelMap::get_voxel`
  wraps an out of range coordinate rather than refusing it, so nothing may reach
  it unchecked.
- Only the child stores the attachment. What is attached *to* a grid is read off
  its children, so the two can never disagree. `set_parent()` on an attached grid
  drops the attachment rather than let the anchor and the parent differ.
- `detach()` moves the grid up one step, to the anchor's own parent, and keeps it
  standing where it was (`transform_relative_to()` in `game/Transform.cpp` redoes the local
  transform against the new parent). Destroying an anchor does the same.
- Saved with the grid: the connector voxels go in its header block in a
  `.bgvox` file, see Grid Files below.

### Grid Files (VoxelFile)

`src/voxel/VoxelFile.cpp/hpp` saves and loads grids as `.bgvox` files. One file
holds a grid and everything hanging off it, flattened: the magic and how many
grids there are, then every grid's readable header, then every grid's binary
body in the same order.

```
BGVOX 4                      <- magic and format version, always line one
grid_count: 2
voxel_bytes: 1
chunk_size: 16

--- GRID ---                 <- one block per grid
id: 0
type: VoxelMap               <- picks the loader
name: my map
description: the world
parent: none
children: 1
palette_size: 12

--- GRID ---
id: 1
type: SingleChunkGrid
name: crate
description: sits on the map
parent: 0
children:
anchor_voxel: 8 3 2          <- attached to the parent at this voxel of it
connector_voxel: 1 1 1       <- held there by this voxel of its own
palette_source: parent       <- shares the parent's colours, so no palette in
palette_size: 0                 its body

--- BINARY ---               <- everything past this line's newline is binary
<body><body>
```

- A **body** is `u32 id, u32 payload_bytes, <payload>`, and a payload is an
  optional palette, the local transform as 10 floats, then whatever
  `VoxelGrid::write_body()` puts there.
- **Ids mean nothing outside the file.** The writer numbers the grids from 0 as
  it walks the tree (`collect_grids`, parents before children), and `parent` /
  `children` in the headers point at those numbers. The bodies carry no
  structure at all - the tree is rebuilt from the headers alone.
- Both directions are written, so either `parent` or `children` alone is enough
  to reassemble. Disagreements are logged and `parent` wins.
- An **attachment** is that parent link plus a connector voxel on each side, so
  only the voxels go in the grid's header: `anchor_voxel` in the parent's
  coordinates, `connector_voxel` in the grid's own. Both lines or neither. They
  are applied once the whole tree is parented, with snapping off, so a grid
  comes back where it was saved rather than being pulled onto its anchor again.
  A grid without them is merely hanging off its parent, which is what every file
  written before this carries - these came in at version 3, as an older build
  keeps unknown keys and simply ignores them. Version 4 changed the VoxelMap
  body (its height and a z per chunk), so version 3 files no longer load.
- **Bodies are found by id, not by position.** They are indexed in one pass
  first (each names its id and its length), so bodies out of header order, or a
  loader that reads the wrong number of bytes, are logged and worked around
  rather than corrupting the grid after them. Neither should ever happen.
- `voxel_file::load_grid()` builds every grid first and hangs them off each
  other afterwards, so a parent that comes later in the file is no trouble. A
  file with no unparented grid, or with a header that has no body, fails
  cleanly and leaves nothing behind. `read_header()` reads only the readable
  part, for listing a directory of files.
- Each grid dispatches on its `type` to the loader registered for it
  (`VoxelMap::load_body`, `SingleChunkGrid::load_body`). New grid types call
  `voxel_file::register_grid_loader()`.
- The body layout is per grid - VoxelMap writes its size (x, y and height)
  and then one block per chunk, each with its chunk x, y and z, SingleChunkGrid writes a single block - but the voxels inside always
  go through `voxel_file::write_chunk`/`read_chunk`, so voxels have the same
  format in every grid. That block is raw or run length encoded, whichever is
  smaller, and carries its own byte count so an unwanted chunk can be skipped.
- Integers are little endian and floats are written as their bit pattern, so
  files move between machines.
- Transforms are written local, so a child comes back in the same place inside
  its parent wherever the root is put.
- A child meshed from its parent's colour map is stored as sharing it and comes
  back on the same `VoxelColourMap`, rather than on an equal copy.
  `load_grid()` also takes one palette to put the whole tree on the scene's
  colours instead of the ones in the file.
- `load_grid()` returns the root, owned by the caller. Nothing is added to a
  VoxelView: use `voxel_file::collect_grids()` to flatten the tree into
  `voxel_grids` (children included, or they are never updated or drawn), and
  `voxel_file::delete_grid_tree()` to free one, as the hierarchy links do not
  own anything.
- No UI for this yet.

### Rendering Pipeline (VoxelView)

Two-pass system in `src/voxel/VoxelView.cpp/hpp`:
1. **Main Pass** - Render with the lighting shader, which traces its own shadows
2. **UI Pass** - Overlay UI elements

Lighting supports directional + point lights. Shaders in `resources/shaders/`
are patched at runtime for the light count, and nothing else.

**Shadows are traced, not mapped.** There is no shadow pass, no shadow map and
no depth comparison, so none of the bias constants that used to be tuned exist
any more:
- The map's voxels are uploaded to a 3D texture (`VoxelVolume`), and
  `lighting.fs` walks that grid from the fragment towards the light, one voxel
  at a time, with Amanatides and Woo's traversal. The first solid voxel it meets
  puts the fragment in shadow; running out of world leaves it lit.
- The walk is mirrored in C++ as `voxel_ray_blocked()` in `game/Picking.cpp`,
  which is what the unit tests cover, as a shader cannot be tested. **Change the
  two together.**
- A shadow ray starts a hundredth of a voxel along the surface normal, so it
  begins in the air voxel in front of the face rather than on the boundary of
  the solid one behind it. Voxel normals are exact, so this holds at any light
  angle. It is the only constant the shadows have.
- `SHADOW_MAX_STEPS` in the shader caps how far a ray travels. A ray that runs
  out is called lit, which is why a very low sun is kept out of the shader
  menu's range: the flatter the angle, the further a ray goes before it clears
  the terrain.
- A directional light is an elevation and an azimuth, with no position and no
  camera of its own. `Light::get_direction()` turns the two into the direction
  the light travels.
- **Every other grid has a brick in `VoxelBrickAtlas`**, traced after the world
  volume, so a vehicle casts a shadow onto the terrain, onto other vehicles and
  onto itself. A grid takes a slot the first time it is uploaded and hands it
  back in `~VoxelGrid`, through `VoxelView::release_grid_volume()`.
- A grid's ray is traced **in the grid's own space**: the shader is handed
  `MatrixInvert` of the grid's world matrix, and inside it the voxels are axis
  aligned again. Nothing is baked per frame, so a turning wheel is exact.
- Which bricks a model is traced against is decided **per draw call**, in
  `VoxelView::sendGridVolumes()`: a grid is sent only when its box, dragged
  along the sun by `SHADOW_CASTER_REACH`, still reaches the model
  (`box_casts_onto()` in `game/Picking.cpp`). At most `MAX_GRID_VOLUMES` of
  them, which `loadAndPatchShader()` patches into the shader so the two sides
  cannot disagree.
- The volumes are bound to texture units `WORLD_VOLUME_TEXTURE_UNIT` (12) and
  `GRID_ATLAS_TEXTURE_UNIT` (13), above the units raylib hands to a material's
  own maps as it draws.
- ShaderMenu's `step view` row colours every fragment by how far its shadow ray
  travelled, green for short and red for long. It is the tool for finding where
  the rays are getting expensive.

**Ambient occlusion is baked into the mesh**, not traced: the mesher writes it
into the vertex colours (see VoxelMesher above) and the shader takes it off the
ambient term in full, plus as much of the direct light as `aoDirectStrength`
asks for. It is per grid, so a vehicle does not darken the ground it stands on -
its sun shadow does that, and the soft contact patch would need either a
per-pixel AO from the volumes or a blob under the vehicle.

### Water

`src/water/WaterView.cpp/hpp` - a flat plane over the whole map at one level,
deliberately **not** part of the voxel system: it is not a grid, not meshed
from voxels, not in the shadow volumes and casts no shadow, and no voxel code
knows it exists. `main.cpp` is the one place the two meet (the map's size, the
VoxelView's camera and the map's world matrix are handed in).

The **level is the simulation's**: `sim::World::water_level`, an `int32_t`
voxel layer (`DEFAULT_WATER_LEVEL` 15, two blocks over the sea floor, never below `MIN_WATER_LEVEL` 0), changed
only by the `SetWaterLevel` command, in `write_state()` and so in the checksum,
and saved in `CORE`. Nothing in the simulation reads it yet.

- A `ViewNode`, a sibling of the VoxelView placed right after it and before the
  UIView. It opens its own `BeginMode3D()` with the VoxelView's camera, which
  only works drawn after the voxels: their depth is still in the buffer, which
  is what hides water behind a hill, and anything see-through goes last.
- The view reads the level through `level_source` (set in `main.cpp` to the
  simulation's) every frame, and the surface is at `level + 1 -
  WATER_SURFACE_INSET` (0.25) in the terrain's Y. The inset keeps it out of the plane
  of a column whose ground top is at the same height, which would z-fight,
  and leaves the waves' crests room under that plane.
- **Attached to the terrain.** The chunks are laid out in the map's own model
  space (X = grid x, Y = grid z, Z = grid y, the mesher's space) and drawn
  through `terrain_matrix`, which `main.cpp` sets to
  `transform_to_matrix(game_map->get_world_transform())` - the same matrix the
  map's chunks are drawn through in `voxel_model_matrix()`. Moving, turning or
  scaling the map carries the water with it. `WaterView::chunk_matrix()` is the
  testable half.
- Cut into `WATER_CHUNK_SIZE` (64) squares by `water_chunk_layout()`, 1600
  of them over the default world. They are
  all the same square, so there is **one shared mesh** (`GenMeshPlane`, a quad
  per unit so a wave shader has vertices to move) drawn once per chunk with
  that chunk's matrix; edge chunks that are cut short are drawn scaled down.
  `set_area()` lays them out again for a world of another size.
- **The sea floor** of the ocean cells, which the voxel map does not hold, is
  drawn here too: `set_floor()` takes a square per ocean cell and a height
  (`start_world()` hands in the sea floor's top), drawn first, opaque in
  `floor_colour`, through the same mesh, shader and culling with the waves
  stilled. Unlit, so its colour is a stand-in for lit stone.
- Only chunks inside the camera's frustum are drawn: `game/Frustum` extracts
  the planes from terrain x `rlGetMatrixModelview()` x `rlGetMatrixProjection()`
  inside the 3D block, so the culling always agrees with what `BeginMode3D()`
  set up, and the planes come out in the terrain's space, where the chunk boxes
  already are.
  The boxes reach `WATER_BOUNDS_MARGIN` above and below the surface, room for
  displaced waves later. `visible_chunk_count()` says how many passed.
- Its own shader, `resources/shaders/water.vs/.fs`, loaded with `LoadShader()`
  (no patching). The fragment half outputs `waterColour` (`WaterView::colour`,
  alpha honoured, 0.7 by default so the ground shows through). Back face culling is off
  while it draws, as the camera can go under the plane.
- **Waves** are made in the vertex half: each vertex is lifted by
  `amplitude * sin(2 pi r / length - 2 pi t / period)`, `r` its distance from
  the terrain's (0, 0), so ripples run outwards from that corner and move with
  the terrain. Per draw the shader gets `chunkRect` (the chunk's centre and
  scale) to turn a mesh vertex back into a terrain position; per frame
  `waveTime`, `waveAmplitude`, `waveNumber` and `waveSpeed`. It also works out
  the slope for `fragNormal`, unused until the fragment half lights anything.
  `water_wave_height()` in `WaterView.cpp` is the C++ twin the tests cover -
  **change the two together**. The time is frame time, so the waves keep
  moving while the game is paused, wrapped once a period so the sine never
  loses precision. `wave_amplitude` / `wave_length` / `wave_period` on the view
  default to 0.2, 8 and 3 s; the amplitude is static_asserted under
  `WATER_SURFACE_INSET` (a crest never reaches the next voxel layer) and
  `WATER_BOUNDS_MARGIN` (the culling never clips a crest).
- The shader is owned by the material: `UnloadMaterial()` in the destructor
  unloads it, while the window is still open (the view tree goes before it).

### UI System

Hand-rolled retained-mode UI in `src/ui`:
- **UIView** (`UIView.cpp/hpp`) - Root of a UI subtree. Resolves layout, dispatches
  mouse events to the topmost element, owns the shared `UIStyle` and the scissor
  clip stack. Exposes `mouse_consumed` so 3D views can skip picking when the
  cursor is over UI.
- **UINode** (`UINode.cpp/hpp`) - Abstract base for UI elements. Parent-relative
  `bounds` + `Anchor` resolved to an absolute `screen_rect` each frame.
  Subclasses implement `draw()` / `measure()` / `on_*` callbacks, not `render()`.
- **Elements** - `UILabel` (text, optional background plate), `UIButton` (text +
  `std::function` action, hover/press states from UIView), `UIImage` (texture,
  sized from one axis plus the aspect ratio; owns the texture when it loaded it).
- **UINumberRow** (`src/ui/UINumberRow.cpp/hpp`) - `label [-] value [+]`. Holds a
  `float*` to a number owned elsewhere, plus its own step, so one row can drive a
  shader uniform, a light setting, or anything else in place.
- **SettingsPanel** (`src/ui/SettingsPanel.cpp/hpp`) - The base of both
  settings menus: a column of UINumberRows on a plate, a toggle key passed to
  its constructor, and `visible` (kept apart from `isEnabled`, see Working
  Guidelines). `add_value_row()` edits a `float*` in place. `add_int_row()`
  takes a getter and a setter instead, for a value the panel cannot write
  directly: the row edits a float of the panel's own, hands the rounded value
  to the setter on every change, and follows the getter only when what it
  returns changes - so a value set by a command, which lands a tick later, does
  not flick back in between, and a loaded game still shows.
- **ShaderMenu** (`src/ui/ShaderMenu.cpp/hpp`) - A SettingsPanel, top left,
  hidden until F3 or its own button. One row per tunable: the ambient level,
  how much ambient occlusion comes off direct light, and the shadow step view,
  all shader uniforms it owns the starting values for. `main.cpp` adds the
  sun's elevation and azimuth rows to it.
- **GameSettingsMenu** (`src/ui/GameSettingsMenu.cpp/hpp`) - A SettingsPanel
  for the world's settings, hidden until F4 or its "Game Settings" button,
  which sits one shader panel's width to the right of "Shader Menu" so both
  panels can be open at once. It has no rows of its own; `main.cpp` adds them.
  The first is the water level (0 to the map's height - 1), which reads
  `simulation->water_level()` and submits `SetWaterLevel`. The second is the
  vehicle distance (16 to 1024), the `EntityManager`'s realise radius, with the
  unrealise radius kept `UNREALIZE_MARGIN` beyond it. It is a getter/setter
  row rather than a pointer, as the entities are freed before the panel. The
  last two are "world cells x/y" (1 to `sim::MAX_WORLD_CELLS`), the size of
  the next world "New Island" makes; they change nothing until then.

The sun's keybind (U) is mirrored by a button in the bottom left, and its angle
in the sky by the shader menu rows; both write the same light. The camera
movement keys (WASD, Q/E, F/C) are held rather than toggled, so they have no
buttons.
- **VehiclePanel** (`src/ui/VehiclePanel.cpp/hpp`) - Top centre, under the
  simulation readout. A free left click on a car (while no other tool owns the
  click, see `world_click_taken`) selects it; the panel shows what the
  simulation says about it, and its Slower / Faster / Reverse / Remove buttons
  queue commands rather than touching anything. The selection is a simulation
  handle, so it survives the car leaving range. Added to the UI before the
  transform menu and the editor so that it is updated before them.
- **VoxelEditor** (`src/ui/VoxelEditor.cpp/hpp`) - A UINode panel in the bottom
  right holding a table of `VoxelPaletteCell`s, one per colour in the grid's
  `voxel_colours` map plus a deselect cell. Pick a colour, then left click the
  world to place a voxel next to the face that was hit, or right click to clear
  the voxel that was hit, on the first grid the ray meets. The armed cell is
  outlined and the target voxel is previewed as a wireframe cube. It skips world
  clicks while `UIView::mouse_consumed` is set.

An element left with a 0 width or height in `bounds` sizes itself through
`measure()`, so most are built with only a margin, e.g. `Rectangle{16, 16, 0, 0}`
with `Anchor::BOTTOM_LEFT`.

Note on raygui: it is fetched by CMake and on the include path, but deliberately
unused. We chose to hand-roll the basic elements (label, button, image) because
raygui is immediate-mode and would duplicate the input state and hit-testing that
UIView already owns. If an expensive widget comes up later - text box with
editing, slider, colour picker, scroll panel - the agreed fallback is to wrap that
single raygui call inside one UINode subclass's `draw()`, keeping UIView in charge
of hit-testing and `mouse_consumed`. Do not convert the framework wholesale.

### Picking

`voxel_model_matrix()` (`src/game/Picking.cpp`) builds the matrix a voxel model
is drawn
with. Both `VoxelView::drawVoxelModel` and `find_voxel_on_ray` go through it, so
what is on screen and what a click hits cannot drift apart. `find_voxel_on_ray`
returns the grid, the ModelInfo, the world space collision and that matrix;
invert the matrix to get model space, where `VoxelGrid::model_to_grid` turns a
point into a grid coordinate and `VoxelGrid::set_voxel` writes it and marks the
right chunk for remeshing. Model space is the mesher's space: X is grid x, Y is
grid z (up), Z is grid y.

### Scripts

`src/game/Script.cpp/hpp` - in-game behaviour run once per frame.

- `Script` is an abstract base with `on_start()`, `on_update(float delta)`
  (seconds, from `GetFrameTime()`) and `get_type()`.
- Scripts are owned globally, in `global::scripts` in `main.hpp`, and are not
  attached to anything: a script that acts on a grid or a light holds its own
  pointer to it. Add one with `global::add_script()`, which stores it and calls
  `on_start()`.
- `mainLoop()` runs every script before `root_view->update()`, so what a script
  changes is seen by that same frame's update and draw. `shutdown()` clears the
  scripts before the view tree, as they may point into it.
- `LambdaScript` wraps an update lambda (and optionally a start one) for
  behaviour too small for its own class. Its state lives in the captures, so it
  can never be saved.
- Saving script state is not done yet (TODO in `Script.hpp`); the plan is a
  write/read pair plus a loader registry keyed on `get_type()`, like the grid
  loaders.
- `main.cpp` adds a test script that spins the small grid (`voxel_grids[1]`).
- **Scripts are cosmetic.** `on_update()` takes the frame time, which is right
  for visuals and wrong for game logic: gameplay belongs in the simulation,
  ticked. An entity can own scripts of its own (`Entity::add_script()`), which
  run in its `present()`; `WheelSpinScript` is the example, reading the
  simulation's speed through its entity.

### Entry Point

`src/game/main.cpp` - Initializes 1600x900 window, sets up shader pipeline, runs 60 FPS main loop. Supports Emscripten/WebAssembly compilation. `mainLoop()` owns the frame's single `BeginDrawing()`/`EndDrawing()` block, so views draw in tree order (3D first, UI on top) - individual ViewNodes must never open their own drawing block.

Each frame, `mainLoop()` runs whole ticks out of `global::tick_accumulator`
(at most `MAX_TICKS_PER_FRAME`, dropping the time past that), calling
`EntityManager::on_tick()` after each step, then `EntityManager::present()`
with the leftover fraction of a tick, then the scripts, then the view tree.
`global::game_speed` scales game time (0 pauses). There is no test scenario
any more (the routes and cars are gone; the commands still work). `init()`
makes a world from a fresh seed (`std::random_device`), `world_cells_x` by
`world_cells_y` cells, so every start is a different island, and
`start_world()` fits everything drawn to the simulation's terrain: the map
resized and drawn, the shadow window over the land, the water's area and the
ocean cells' sea floor, and the camera over the middle of the land. It runs
after a load too. The "New Island" button (bottom left) calls `new_world()`,
which replaces the game as a load does; the game settings menu's "world cells
x/y" rows set the size it uses. A readout under the title shows the tick, the
ticks run that frame, vehicles in the simulation against vehicles drawn, and
the world's seed, shape, elevation and biome ("loaded game" after a load, as
the seed is not saved).

Shutdown order matters: scripts, then entities (their grids are still in the
VoxelView, which takes them back), then the shader, the view tree - whose
WaterView frees its mesh and shader, and whose VoxelView deletes the grids it still holds, which by then are only its own map
and test grid - and then the simulation.

`global::shutdown()` unloads the voxel shader and then clears its `locs` and
`id` by hand. `raylib::Shader` is an owning wrapper whose destructor unloads
again at static destruction - after the window is closed - unless `locs` is
null, and the free `UnloadShader()` takes its argument by value, so it cannot
null it for you. Without that clearing the program aborts on exit with a double
free. The same trap waits for any other raylib-cpp owning wrapper that is also
unloaded by hand.

It holds `main()`, the `global::` state and the shader loading, and nothing else:
it is the only file outside `business_game_lib`, so anything the rest of the code
has to call cannot live here. The transform maths is in `src/game/Transform.cpp`
and the ray casts in `src/game/Picking.cpp` for that reason. `main.hpp` still
includes both, so an existing include of it keeps working, but new code should
take the narrow header instead of dragging in the window and the view tree.

## Working Guidelines

- Only edit what is explicitly requested - do not refactor or "improve" surrounding code
- `ViewNode::isEnabled` currently stops the whole sibling chain, not just that
  node: a disabled node returns before recursing to its sibling, so everything
  added after it also stops updating and drawing. Hide a node with its own flag
  (as ShaderMenu does with `visible`) until that is sorted out.
- Look for `TODO(claude)` comments in the codebase for tasks to pick up
- Do not compile or run the program - the user will handle building and testing
- Add appropriate comments to explain non-obvious logic
- Never use emojis in code or string literals
- When the user wants to plan rather than implement, present available options and trade-offs before writing code

## Key Dependencies

- **raylib** - Graphics/rendering
- **raylib-cpp** - C++ wrapper for raylib
- **raygui** - UI components
- **PerlinNoise.hpp** - The reference `sim::PerlinNoise` was ported from, used
  only by `test_terrain_voxels.cpp` now (in `includes/`)

## Known Issues (from TODOs in code)

- Greedy meshing optimization not yet implemented
- VoxelGrid model vector recreated on every call (VoxelGrid.hpp:71)
- A placeholder car is five grids, so it takes five shadow atlas slots (256 in
  all) and five of a draw call's `MAX_GRID_VOLUMES` (8) casters. Two cars side
  by side already overflow that, and the casters past the eighth cast no shadow
  on that draw. A real fleet wants either one brick per vehicle or a larger cap.
- An island is thousands of voxel chunks (a two by two mountainous one is up to
  about 12000, most of them buried and never drawn), all meshed on the first
  frame after `start_world()`, and every one on the surface is a draw call.
  Greedy meshing and chunk-level culling are what would bring both down.
- The sea floor plane is unlit, so it does not match lit stone exactly where
  an ocean cell meets an island's own sea floor; and it is not a grid, so the
  editor cannot pick it.
- The camera keeps its old speed (24 units a second), which crosses a 2560
  voxel world in under two minutes.
- `TestHelpers`' `TempDir` names its directories from a counter per process,
  so two test processes run in parallel (`ctest -j`) can collide on
  `business_game_tests_0`. Serial runs are fine.
- Moving an entity's root grid with the GridTransformMenu does nothing lasting:
  the next `present()` puts it back where the simulation says.
- `apply_transform_rot()` composes two rotations with `QuaternionAdd`, where
  composing is `QuaternionMultiply` - what `transform_transform()` correctly
  uses. Two quarter turns come out as something that is not a half turn and is
  not even a unit quaternion. Only `apply_transform()` calls it and nothing calls
  that, so nothing is visibly wrong today. Pinned as it stands by a test in
  `test_transform.cpp`, which is the one to update if it is fixed.
