# Terrain

This document will go over how the game map will be created and modified, first from a gameplay perspective then go into implementation details.

## Cells & the island system

The world map is split into cells which are 64x64x64 blocks big. A cell functions similarly to the concept of a chunk, but because we already use the word 'chunk' on the voxel layer, we will use the word 'cell' on the simulation block layer.

Multiple cells make up islands, which can be of varying sizes. Some islands will be generated, and some islands will be prebuilt and will be loaded from the disk. During gameplay, players will be able to purchase new islands into existence and place on empty cells. Because of this, each cell that isn't an island will be a default ocean cell that has only a flat layer of blocks below sea level.  

The cells that make up an island is called the 'footprint' of that island. Islands footprints should be like the Tetris blocks.

## Island generation

Before the terrain is generated, the following attributes of the island are decided (either randomly or through a gameplay system):
- Footprint: L-shaped, square, T-shaped, I-shaped, zigzag
- Elevation type: flat, hilly, mountainous 
- Biome: grassland, desert, snowy → these decide the colour of the blocks
- Other gameplay attributes: inhabeted/uninhabited, resources etc. (for the future)

