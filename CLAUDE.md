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

There are three targets. `business_game_lib` holds everything except `main()`,
`business_game` is `main.cpp` linked against it, and `business_game_tests` is the
Catch2 runner linked against the same library. **A new source file goes in the
library's list, not the executable's** - the executable is only `main.cpp`.

Pass `-DBUSINESS_GAME_BUILD_TESTS=OFF` to skip building Catch2, which is off
automatically for a web build.

## Tests

Catch2 v3 (`src/tests/`), one file per area. They run without a window, which is
what limits what they can cover:

- `test_transform.cpp` - the maths in `game/Transform.hpp`.
- `test_attachment.cpp` - the grid hierarchy and the attachment system.
- `test_voxel_file.cpp` - the `.bgvox` chunk encoding, scalars and whole files.
- `test_voxel_mesh.cpp` - `build_chunk_mesh_data()`: which faces come out, the
  faces dropped against a neighbouring chunk, and the baked ambient occlusion.
- `test_voxel_ray.cpp` - `voxel_ray_blocked()`, the voxel walk a shadow ray
  does, which is the testable twin of the one in `lighting.fs`.
- `TestHelpers.hpp` - a palette, a self-deleting temp directory, and the
  `REQUIRE_VEC3_EQ` / `REQUIRE_QUAT_EQ` / `REQUIRE_TRANSFORM_EQ` comparisons.
  Include it **first** in a test file: `raymath.h` redefines raylib's vector
  types unless `raylib.h` is in ahead of it, and this header gets that order
  right.

Grids in tests are built with a null `VoxelView`. That is only safe because
`update_models()` is the one thing that dereferences it, and it is also the only
call that needs an OpenGL context, so **tests must never call `update_models()`**.
Anything that meshes, draws or reads input is out of reach and is not covered:
`VoxelMesher`, `VoxelView`, `Light`, and all of `src/ui`.

## Architecture

### Scene Graph (ViewNode Tree)

The UI/scene uses a `ViewNode` tree hierarchy with recursive update/render traversal:
- `root_view` → `VoxelView` (→ `VoxelEditor`) and `UIView` (→ UI elements),
  where `UIView` is a sibling of `VoxelView` and so is updated and drawn after it
- ViewNodes manage parent/child/sibling relationships
- Located in `src/game/ViewNode.hpp`
- `VoxelEditor` lives in `src/ui/`, `VoxelView` in `src/voxel/`

### Voxel System

**VoxelGrid** (abstract base in `src/voxel/VoxelGrid.hpp`) has two implementations:
- **VoxelMap** (`src/voxel/VoxelMap.cpp/hpp`) - Chunk-based storage (16x16x16 chunks), Perlin noise terrain generation
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
textures.

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
BGVOX 3                      <- magic and format version, always line one
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
  written before this carries - the version is still 3, as an older build keeps
  unknown keys and simply ignores these.
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
- The body layout is per grid - VoxelMap writes its size and then one block per
  chunk, SingleChunkGrid writes a single block - but the voxels inside always
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
- **Only the map is in the volume.** A grid with its own transform (a vehicle)
  does not cast a shadow and does not shadow itself yet: that needs a volume per
  grid, marched in the grid's own space.
- The volume is bound to texture unit `WORLD_VOLUME_TEXTURE_UNIT` (12), above
  the units raylib hands to a material's own maps as it draws.
- ShaderMenu's `step view` row colours every fragment by how far its shadow ray
  travelled, green for short and red for long. It is the tool for finding where
  the rays are getting expensive.

**Ambient occlusion is baked into the mesh**, not traced: the mesher writes it
into the vertex colours (see VoxelMesher above) and the shader takes it off the
ambient term in full, plus as much of the direct light as `aoDirectStrength`
asks for. It is per grid, so a vehicle does not darken the ground it stands on -
its sun shadow does that, and the soft contact patch would need either a
per-pixel AO from the volumes or a blob under the vehicle.

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
- **ShaderMenu** (`src/ui/ShaderMenu.cpp/hpp`) - Debug panel, top left, hidden
  until F3 or its own button. One UINumberRow per tunable: the ambient level,
  how much ambient occlusion comes off direct light, and the shadow step view,
  all shader uniforms it owns the starting values for.
  `add_value_row()` hangs non-uniform values off the same panel, which is how
  the sun's elevation and azimuth rows are added in `main.cpp`.

The sun's keybind (U) is mirrored by a button in the bottom left, and its angle
in the sky by the shader menu rows; both write the same light. The camera
movement keys (WASD, Q/E, F/C) are held rather than toggled, so they have no
buttons.
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

### Entry Point

`src/game/main.cpp` - Initializes 1600x900 window, sets up shader pipeline, runs 60 FPS main loop. Supports Emscripten/WebAssembly compilation. `mainLoop()` owns the frame's single `BeginDrawing()`/`EndDrawing()` block, so views draw in tree order (3D first, UI on top) - individual ViewNodes must never open their own drawing block.

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
- **PerlinNoise.hpp** - Terrain generation (in `includes/`)

## Known Issues (from TODOs in code)

- Greedy meshing optimization not yet implemented
- VoxelGrid model vector recreated on every call (VoxelGrid.hpp:71)
- `apply_transform_rot()` composes two rotations with `QuaternionAdd`, where
  composing is `QuaternionMultiply` - what `transform_transform()` correctly
  uses. Two quarter turns come out as something that is not a half turn and is
  not even a unit quaternion. Only `apply_transform()` calls it and nothing calls
  that, so nothing is visibly wrong today. Pinned as it stands by a test in
  `test_transform.cpp`, which is the one to update if it is fixed.
