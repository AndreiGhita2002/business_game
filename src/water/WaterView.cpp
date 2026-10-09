//
// Created by Claude on 09.10.2026.
//

#include "WaterView.hpp"

#include <algorithm>
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
      chunks(water_chunk_layout(size_x, size_z, WATER_CHUNK_SIZE))
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

    // Draws every square of `squares` the camera can see at height y, with
    // `mesh`, or with `far_mesh` (when there is one) where the whole square
    // is past the fade, which also means its edges are flat wherever it meets
    // a square drawn in full. The colour and amplitude are whatever the
    // shader was last given.
    full_detail_last_frame = 0;
    const auto draw_squares = [this, &terrain, &eye, &fade](const Mesh& mesh, const Mesh* far_mesh,
                                                           const Frustum& frustum,
                                                           const std::vector<WaterChunk>& squares, const float y) {
        size_t visible = 0;
        for (const WaterChunk& chunk : squares) {
            const BoundingBox box = water_chunk_bounds(chunk, y);
            if (!frustum_contains_box(frustum, box)) continue;

            const Mesh* drawn = &mesh;
            if (far_mesh != nullptr) {
                if (distance_to_box(eye, transform_box(box, terrain)) >= fade[1]) drawn = far_mesh;
                else full_detail_last_frame++;
            }

            // The same placement chunk_matrix() makes, as numbers, so the
            // shader can find each vertex on the terrain for its wave
            const Vector4 rect = {
                chunk.x + chunk.width * 0.5f, chunk.z + chunk.depth * 0.5f,
                chunk.width / WATER_CHUNK_SIZE, chunk.depth / WATER_CHUNK_SIZE,
            };
            SetShaderValue(material.shader, chunk_rect_loc, &rect, SHADER_UNIFORM_VEC4);
            DrawMesh(*drawn, material, chunk_matrix(chunk, y, terrain));
            visible++;
        }
        return visible;
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
            draw_squares(flat_mesh, nullptr, frustum, floor_rects, floor_height);
        }

        const Vector4 colour_normalised = ColorNormalize(colour);
        SetShaderValue(material.shader, colour_loc, &colour_normalised, SHADER_UNIFORM_VEC4);
        SetShaderValue(material.shader, wave_amplitude_loc, &amplitude, SHADER_UNIFORM_FLOAT);
        visible_last_frame = draw_squares(chunk_mesh, &flat_mesh, frustum, chunks, surface_y);

        rlEnableBackfaceCulling();
    }
    EndMode3D();

    // Whatever comes after this view (the UI) is drawn on top of the water
    ViewNode::render();
}
