# Voxel Grids Design

This document goes what voxel grids should be capable of doing and how a user is allowed to edit them. 

## Voxel controls

This section goes over a user/system/script should be able to modify a grid. Note that in the actual game, the user might not be allowed to edit certain grids due to various gameplay reasons. This section is more of a checklist of things that should be possible in the engine, as well as be used by devs in order to build grid assets. These controls should be generalised to all grid implementations    

This is how voxel grids should be able to be modified:
-[x] place voxels 
-[x] remove voxels
-[ ] be moved
  -[ ] translation
  -[ ] rotation
-[x] be able to be attached to a different grid
  - grid attachment is a hierarchical affair; as in the attached grid becomes a child of the other grid in the scene graph.  
  - when a grid is attached to another grid, a voxel from each grid is decided to be a connector voxel. Removal of this connector should not be allowed unless the grids get detached
  - the parent transform should apply to the child when calculating its position, but the child transform should not apply to the parent
  - the goal of this attachment system is to have voxel based vehicles that have parts that are part of different grids which move both with the vehicle but also on their own; imagine a car that has attached wheels on different grids, and the wheels spin as the car moves but also move with the car. 
  - `VoxelGrid::attach_to(anchor, anchor_voxel, local_voxel)`. Both connectors
    have to be solid voxels, and an anchor already below the grid is refused.
    The grid is snapped so the two connector voxels sit in the same place;
    turning it afterwards and calling `snap_to_anchor()` again keeps it there,
    which is what a spinning wheel does.
-[x] be able to be detached from parent grid
  - should stop being a child of said parent and gain the same parent as their old parent (move up in the hierarchy) 
  - `VoxelGrid::detach()`. The grid keeps the place it was standing in, and
    both connector voxels become ordinary voxels again.
  - Still to do: no UI for either, and an attachment is not written to a
    `.bgvox` file yet, so a saved tree comes back parented but not attached.
-[ ] be modified by runtime scripts/systems
  - more on this when we decide how to implement these

This is how grids should not be able to be modified:
- change any other feature that is specific to a grid implementation, like changing chunk boundaries for VoxelMap. 

## Voxel Visuals

Voxels should be monochrome cubes. We rely on shaders and the builds themselves for our aesthetic.

- [ ] In the future, there should be a way of placing text on the surface of grids. This should probably not be implemented as a VoxelGrid, rather a different type of rendering entity thingy.   
