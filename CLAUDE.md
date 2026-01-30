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
- `root_view` → `VoxelView` → `VoxelEditor`
- ViewNodes manage parent/child/sibling relationships
- Located in `src/game/ViewNode.hpp`

### Voxel System

**VoxelGrid** (abstract base in `src/voxel/VoxelGrid.hpp`) has two implementations:
- **VoxelMap** (`src/voxel/VoxelMap.cpp/hpp`) - Chunk-based storage (16x16x16 chunks), Perlin noise terrain generation
- **SingleChunkGrid** (`src/voxel/SingleChunkGrid.cpp/hpp`) - Single chunk for the voxel editor

**VoxelMesher** (`src/voxel/VoxelMesher.cpp/hpp`) converts voxel data to 3D meshes with per-material generation.

### Rendering Pipeline (VoxelView)

Three-pass system in `src/game/VoxelView.cpp/hpp`:
1. **Shadow Pass** - Render to shadow map for each light
2. **Main Pass** - Render with lighting shader using shadow maps
3. **UI Pass** - Overlay UI elements

Lighting system supports directional + point lights with 1024x1024 shadow maps. Shaders in `resources/shaders/` are patched at runtime for dynamic light count.

### Entry Point

`src/game/main.cpp` - Initializes 1600x900 window, sets up shader pipeline, runs 60 FPS main loop. Supports Emscripten/WebAssembly compilation.

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
