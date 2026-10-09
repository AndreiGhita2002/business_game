//
// Created by Claude on 09.10.2026.
//

#include "WaterView.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <raymath.h>
#include <rlgl.h>

#include "game/Frustum.hpp"

std::vector<WaterChunk> water_chunk_layout(const int size_x, const int size_z, const int chunk_size) {
    std::vector<WaterChunk> chunks;
    if (size_x <= 0 || size_z <= 0 || chunk_size <= 0) return chunks;

    for (int z = 0; z < size_z; z += chunk_size) {
        for (int x = 0; x < size_x; x += chunk_size) {
            chunks.push_back(WaterChunk{
                static_cast<float>(x),
                static_cast<float>(z),
                static_cast<float>(std::min(chunk_size, size_x - x)),
                static_cast<float>(std::min(chunk_size, size_z - z)),
            });
        }
    }
    return chunks;
}

float water_surface_height(const int level) {
    // Layer n of the voxels runs from n to n + 1 in the terrain's Y
    return static_cast<float>(level + 1) - WATER_SURFACE_INSET;
}

BoundingBox water_chunk_bounds(const WaterChunk& chunk, const float surface_y) {
    return BoundingBox{
        Vector3{chunk.x, surface_y - WATER_BOUNDS_MARGIN, chunk.z},
        Vector3{chunk.x + chunk.width, surface_y + WATER_BOUNDS_MARGIN, chunk.z + chunk.depth},
    };
}

float water_wave_height(const float x, const float z, const float time,
                        const float amplitude, const float length, const float period) {
    if (length <= 0.0f || period <= 0.0f) return 0.0f;
    const float r = std::sqrt(x * x + z * z);
    return amplitude * std::sin(2.0f * PI * r / length - 2.0f * PI * time / period);
}

float water_wave_fade(const float distance, const float near, const float far) {
    if (far <= near) return distance < near ? 1.0f : 0.0f;
    return std::clamp((far - distance) / (far - near), 0.0f, 1.0f);
}

float distance_to_box(const Vector3 point, const BoundingBox box) {
    const Vector3 nearest = Vector3Clamp(point, box.min, box.max);
    return Vector3Distance(point, nearest);
}

BoundingBox transform_box(const BoundingBox box, const Matrix matrix) {
    BoundingBox out{};
    for (int i = 0; i < 8; ++i) {
        const Vector3 corner = Vector3Transform(Vector3{
            (i & 1) ? box.max.x : box.min.x,
            (i & 2) ? box.max.y : box.min.y,
            (i & 4) ? box.max.z : box.min.z,
        }, matrix);
        if (i == 0) {
            out.min = corner;
            out.max = corner;
        } else {
            out.min = Vector3Min(out.min, corner);
            out.max = Vector3Max(out.max, corner);
        }
    }
    return out;
}

WaterView::WaterView(ViewNode* parent, const raylib::Camera* camera, const int size_x, const int size_z,
                     const std::string& shader_path)
    : ViewNode(parent), camera(camera),
      chunks(water_chunk_layout(size_x, size_z, WATER_CHUNK_SIZE)),
      cells(water_cell_layout(size_x, size_z, WATER_CELL_SIZE, WATER_CHUNK_SIZE))
{
    chunk_mesh = GenMeshPlane(WATER_CHUNK_SIZE, WATER_CHUNK_SIZE, WATER_CHUNK_SIZE, WATER_CHUNK_SIZE);
    // The same square as one quad, for what is flat: the sea floor, which is
    // still, and water far enough off that its waves have faded out
    flat_mesh = GenMeshPlane(WATER_CHUNK_SIZE, WATER_CHUNK_SIZE, 1, 1);

    // LoadShader() falls back to raylib's default shader if either file fails,
    // and logs why, so a broken water shader shows up as untinted white water
    // rather than as no water at all.
    const std::string vertex_path = shader_path + ".vs";
    const std::string fragment_path = shader_path + ".fs";
    material = LoadMaterialDefault();
    material.shader = LoadShader(vertex_path.c_str(), fragment_path.c_str());
    colour_loc = GetShaderLocation(material.shader, "waterColour");
    chunk_rect_loc = GetShaderLocation(material.shader, "chunkRect");
    wave_time_loc = GetShaderLocation(material.shader, "waveTime");
    wave_amplitude_loc = GetShaderLocation(material.shader, "waveAmplitude");
    wave_number_loc = GetShaderLocation(material.shader, "waveNumber");
    wave_speed_loc = GetShaderLocation(material.shader, "waveSpeed");
    wave_fade_loc = GetShaderLocation(material.shader, "waveFade");
    camera_position_loc = GetShaderLocation(material.shader, "cameraPosition");
    fog_colour_loc = GetShaderLocation(material.shader, "fogColour");
    fog_range_loc = GetShaderLocation(material.shader, "fogRange");
}

WaterView::~WaterView() {
    UnloadMesh(chunk_mesh);
    UnloadMesh(flat_mesh);
    // Unloads the water shader with it, as it is not raylib's default one
    UnloadMaterial(material);
}

std::string& WaterView::get_view_type() {
    static std::string TYPE = WATER_VIEW_STR;
    return TYPE;
}

void WaterView::update(const float delta_time) {
    wave_time += delta_time;
    if (wave_period > 0.0f) wave_time = std::fmod(wave_time, wave_period);

    // On to the siblings, the UIView among them
    ViewNode::update(delta_time);
}

Matrix WaterView::chunk_matrix(const WaterChunk& chunk, const float surface_y, const Matrix terrain) {
    // The shared mesh is a full chunk centred on the origin: scale it down to
    // a cut short chunk, move it to the chunk's centre, and then put the lot
    // where the terrain is. raylib's MatrixMultiply applies the left one first.
    const Matrix local = MatrixMultiply(
        MatrixScale(chunk.width / WATER_CHUNK_SIZE, 1.0f, chunk.depth / WATER_CHUNK_SIZE),
        MatrixTranslate(chunk.x + chunk.width * 0.5f, surface_y, chunk.z + chunk.depth * 0.5f));
    return MatrixMultiply(local, terrain);
}

void WaterView::set_area(const int size_x, const int size_z) {
    chunks = water_chunk_layout(size_x, size_z, WATER_CHUNK_SIZE);
    cells = water_cell_layout(size_x, size_z, WATER_CELL_SIZE, WATER_CHUNK_SIZE);
}

std::vector<WaterCell> water_cell_layout(const int size_x, const int size_z, const int cell_size,
                                         const int chunk_size) {
    std::vector<WaterCell> cells;
    if (size_x <= 0 || size_z <= 0 || chunk_size <= 0 || cell_size <= 0 || cell_size % chunk_size != 0)
        return cells;

    // The chunks are laid out row by row (water_chunk_layout()), so chunk
    // (i, j) is number i + j * chunks_per_row
    const int chunks_per_row = (size_x + chunk_size - 1) / chunk_size;
    for (const WaterChunk& rect : water_chunk_layout(size_x, size_z, cell_size)) {
        WaterCell cell{rect, {}};
        const int i0 = static_cast<int>(rect.x) / chunk_size;
        const int j0 = static_cast<int>(rect.z) / chunk_size;
        const int i1 = (static_cast<int>(rect.x + rect.width) + chunk_size - 1) / chunk_size;
        const int j1 = (static_cast<int>(rect.z + rect.depth) + chunk_size - 1) / chunk_size;
        for (int j = j0; j < j1; ++j) {
            for (int i = i0; i < i1; ++i) cell.chunks.push_back(static_cast<size_t>(i + j * chunks_per_row));
        }
        cells.push_back(std::move(cell));
    }
    return cells;
}

void WaterView::set_floor(std::vector<WaterChunk> rects, const float height) {
    floor_rects = std::move(rects);
    floor_height = height;
}

void WaterView::render() {
    const float surface_y = water_surface_height(level_source ? level_source() : 0);
    const Matrix terrain = terrain_matrix ? terrain_matrix() : MatrixIdentity();

    // The shader takes the wave as a wave number and an angular speed, which
    // saves it a division per vertex. A length or period of 0 stills the water.
    const bool waves_on = wave_length > 0.0f && wave_period > 0.0f;
    const float wave_number = waves_on ? 2.0f * PI / wave_length : 0.0f;
    const float wave_speed = waves_on ? 2.0f * PI / wave_period : 0.0f;
    const float amplitude = waves_on ? wave_amplitude : 0.0f;
    SetShaderValue(material.shader, wave_time_loc, &wave_time, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material.shader, wave_number_loc, &wave_number, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material.shader, wave_speed_loc, &wave_speed, SHADER_UNIFORM_FLOAT);

    // The waves fade out with distance from the camera (water_wave_fade()),
    // so far water is flat and can be drawn as one quad
    const Vector3 eye = camera->position;
    const float fade[2] = {wave_fade_near, std::max(wave_fade_far, wave_fade_near + 1.0f)};
    SetShaderValue(material.shader, camera_position_loc, &eye, SHADER_UNIFORM_VEC3);
    SetShaderValue(material.shader, wave_fade_loc, fade, SHADER_UNIFORM_VEC2);

    // The fog, the same as the voxels' so the shore and the sea fade together
    const Fog none{BLANK, FLT_MAX, FLT_MAX};
    const Fog& f = fog != nullptr ? *fog : none;
    const Vector4 fog_colour = ColorNormalize(f.colour);
    const float fog_range[2] = {f.start, f.end};
    SetShaderValue(material.shader, fog_colour_loc, &fog_colour, SHADER_UNIFORM_VEC3);
    SetShaderValue(material.shader, fog_range_loc, fog_range, SHADER_UNIFORM_VEC2);

    // One square drawn with `mesh` at height y. The same placement
    // chunk_matrix() makes is handed to the shader as numbers, so it can find
    // each vertex on the terrain for its wave. The colour and amplitude are
    // whatever the shader was last given.
    const auto draw_square = [this, &terrain](const Mesh& mesh, const WaterChunk& square, const float y) {
        const Vector4 rect = {
            square.x + square.width * 0.5f, square.z + square.depth * 0.5f,
            square.width / WATER_CHUNK_SIZE, square.depth / WATER_CHUNK_SIZE,
        };
        SetShaderValue(material.shader, chunk_rect_loc, &rect, SHADER_UNIFORM_VEC4);
        DrawMesh(mesh, material, chunk_matrix(square, y, terrain));
    };
    // Whether every point of a box is past the waves' fade, which means it is
    // flat, edges included, wherever it meets water drawn in full
    const auto past_fade = [&terrain, &eye, &fade](const BoundingBox& box) {
        return distance_to_box(eye, transform_box(box, terrain)) >= fade[1];
    };

    BeginMode3D(*camera); {
        // Taken from rlgl rather than worked out from the camera again, so the
        // culling always agrees with the projection BeginMode3D() set up. The
        // terrain's matrix goes in front, which puts the planes in the
        // terrain's own space: the chunks' boxes are tested where they are
        // laid out, and a moved, turned or scaled map needs no boxes rebuilt.
        const Frustum frustum = frustum_from_matrix(MatrixMultiply(
            terrain, MatrixMultiply(rlGetMatrixModelview(), rlGetMatrixProjection())));

        // The plane has one side, and the camera can be taken below it
        rlDisableBackfaceCulling();

        // The sea floor first: it is opaque, and the water over it is not.
        // Still, so it stays flat, and drawn with the water's own shader.
        if (!floor_rects.empty()) {
            const Vector4 floor_normalised = ColorNormalize(floor_colour);
            const float still = 0.0f;
            SetShaderValue(material.shader, colour_loc, &floor_normalised, SHADER_UNIFORM_VEC4);
            SetShaderValue(material.shader, wave_amplitude_loc, &still, SHADER_UNIFORM_FLOAT);
            for (const WaterChunk& square : floor_rects) {
                if (frustum_contains_box(frustum, water_chunk_bounds(square, floor_height)))
                    draw_square(flat_mesh, square, floor_height);
            }
        }

        const Vector4 colour_normalised = ColorNormalize(colour);
        SetShaderValue(material.shader, colour_loc, &colour_normalised, SHADER_UNIFORM_VEC4);
        SetShaderValue(material.shader, wave_amplitude_loc, &amplitude, SHADER_UNIFORM_FLOAT);

        // A cell at a time: a whole cell past the fade is one flat quad,
        // which is most of the sea once the camera can see thousands of
        // voxels out. Nearer than that, its chunks are drawn one by one, each
        // in full where the waves reach it and flat where they have faded.
        size_t draws = 0;
        full_detail_last_frame = 0;
        for (const WaterCell& cell : cells) {
            const BoundingBox cell_box = water_chunk_bounds(cell.rect, surface_y);
            if (!frustum_contains_box(frustum, cell_box)) continue;
            if (past_fade(cell_box)) {
                draw_square(flat_mesh, cell.rect, surface_y);
                draws++;
                continue;
            }
            for (const size_t i : cell.chunks) {
                const WaterChunk& chunk = chunks[i];
                const BoundingBox box = water_chunk_bounds(chunk, surface_y);
                if (!frustum_contains_box(frustum, box)) continue;
                const bool flat = past_fade(box);
                draw_square(flat ? flat_mesh : chunk_mesh, chunk, surface_y);
                if (!flat) full_detail_last_frame++;
                draws++;
            }
        }
        visible_last_frame = draws;

        rlEnableBackfaceCulling();
    }
    EndMode3D();

    // Whatever comes after this view (the UI) is drawn on top of the water
    ViewNode::render();
}
