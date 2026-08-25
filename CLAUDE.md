# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Business Game is a work-in-progress tycoon game inspired by OpenTTD, built with a custom voxel-based 3D rendering engine in C++20.

## Build Commands

```bash
# Build the project
mkdir build && cd build
cmake ..
cmake --build .

# Executable location
build/bin/business_game
```

The project uses CMake with FetchContent for dependencies (raylib, raylib-cpp, raygui). No test framework is configured.

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

**VoxelMesher** (`src/voxel/VoxelMesher.cpp/hpp`) converts voxel data to 3D meshes with per-material generation.

### Rendering Pipeline (VoxelView)

Three-pass system in `src/voxel/VoxelView.cpp/hpp`:
1. **Shadow Pass** - Render to shadow map for each light
2. **Main Pass** - Render with lighting shader using shadow maps
3. **UI Pass** - Overlay UI elements

Lighting system supports directional + point lights with 1024x1024 shadow maps. Shaders in `resources/shaders/` are patched at runtime for dynamic light count.

Shadow pass details worth knowing:
- A light's `light_camera` is orthographic, and raylib reads `fovy` on an ortho
  camera as the **height of the box in world units**, not an angle. That value is
  the size of the region that gets shadows at all - fragments outside the light
  frustum are drawn fully lit (`lighting.fs`), which looks like shadows simply
  stopping partway across the scene.
- The pass renders **back faces only** (`RL_CULL_FACE_FRONT`), so the depth in
  the map is the far side of a solid and a surface cannot shadow itself. This
  depends on the voxel meshes being closed.
- It uses its own tight clip planes (`SHADOW_NEAR`/`SHADOW_FAR` in `Light.hpp`)
  and restores raylib's defaults afterwards.
- The shader's bias is counted in shadow map texels; `Light::update` sends
  `shadowTexelDepth` per light, so changing a light's box needs no retuning.
- `biasTexels`, `biasSlopeTexels` and `biasMaxSlope` are uniforms with no
  defaults in the shader. ShaderMenu sends them when it is built, so the shader
  needs a ShaderMenu (or an equivalent) to light anything correctly.

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
  until F3 or its own button. One UINumberRow per tunable: the three shadow bias
  uniforms, the ambient level, and each light's shadow box size. It owns the
  starting values for the bias uniforms, which `lighting.fs` no longer defines
  itself. `add_value_row()` hangs non-uniform values off the same panel.

Every light keybind (Y, U, I, O, P) is mirrored by a button in the bottom left
or a row in the shader menu; both write the same state. The camera movement keys
(WASD, Q/E, F/C) are held rather than toggled, so they have no buttons.
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

`voxel_model_matrix()` (`main.cpp`) builds the matrix a voxel model is drawn
with. Both `VoxelView::drawVoxelModel` and `find_voxel_on_ray` go through it, so
what is on screen and what a click hits cannot drift apart. `find_voxel_on_ray`
returns the grid, the ModelInfo, the world space collision and that matrix;
invert the matrix to get model space, where `VoxelGrid::model_to_grid` turns a
point into a grid coordinate and `VoxelGrid::set_voxel` writes it and marks the
right chunk for remeshing. Model space is the mesher's space: X is grid x, Y is
grid z (up), Z is grid y.

### Entry Point

`src/game/main.cpp` - Initializes 1600x900 window, sets up shader pipeline, runs 60 FPS main loop. Supports Emscripten/WebAssembly compilation. `mainLoop()` owns the frame's single `BeginDrawing()`/`EndDrawing()` block, so views draw in tree order (3D first, UI on top) - individual ViewNodes must never open their own drawing block.

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

- Double-free bug with UnloadShader (commented out in main.cpp:52)
- The mesher treats out-of-chunk neighbours as air, so every chunk emits a wall
  of hidden faces along its borders. Stitching neighbouring chunks would drop
  them (noted in `VoxelMesher.cpp`).
- Greedy meshing optimization not yet implemented
- VoxelGrid model vector recreated on every call (VoxelGrid.hpp:71)
