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

### Entry Point

`src/game/main.cpp` - Initializes 1600x900 window, sets up shader pipeline, runs 60 FPS main loop. Supports Emscripten/WebAssembly compilation. `mainLoop()` owns the frame's single `BeginDrawing()`/`EndDrawing()` block, so views draw in tree order (3D first, UI on top) - individual ViewNodes must never open their own drawing block.

## Working Guidelines

- Only edit what is explicitly requested - do not refactor or "improve" surrounding code
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
- Ray-grid collision incomplete (main.cpp:103)
- Greedy meshing optimization not yet implemented
- VoxelGrid model vector recreated on every call (VoxelGrid.hpp:71)
