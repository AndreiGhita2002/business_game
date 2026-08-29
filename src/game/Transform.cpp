//
// Created by Andrei Ghita on 29.08.2026.
//

#include "game/Transform.hpp"

#include <iostream>
#include <raymath.h>

Vector3 apply_transform_trans(const Vector3 v, const Transform &t) {
    // Scale
    Vector3 scaled = {
        v.x * t.scale.x,
        v.y * t.scale.y,
        v.z * t.scale.z
    };

    // Rotate
    Vector3 rotated = Vector3RotateByQuaternion(scaled, t.rotation);

    // Translate
    return Vector3Add(rotated, t.translation);
}

Quaternion apply_transform_rot(Quaternion rot, const Transform &t) {
    return QuaternionAdd(rot, t.rotation);
}

Vector3 apply_transform_scale(Vector3 scale, const Transform &t) {
    return Vector3{
        scale.x * t.scale.x,
        scale.y * t.scale.y,
        scale.z * t.scale.z
    };
}

void apply_transform(Vector3* position, Quaternion* rotation, Vector3* scale, const Transform& t) {
    *position = apply_transform_trans(*position, t);
    *rotation = apply_transform_rot(*rotation, t);
    *scale = apply_transform_scale(*scale, t);
}
Transform transform_transform(const Transform &base, const Transform &applied) {
    Transform result;

    result.scale.x = base.scale.x * applied.scale.x;
    result.scale.y = base.scale.y * applied.scale.y;
    result.scale.z = base.scale.z * applied.scale.z;

    result.rotation = QuaternionMultiply(applied.rotation, base.rotation);

    Vector3 scaled = Vector3Multiply(base.translation, applied.scale);
    Vector3 rotated = Vector3RotateByQuaternion(scaled, applied.rotation);
    result.translation = Vector3Add(rotated, applied.translation);

    return result;
}

Transform transform_relative_to(const Transform &world, const Transform &parent_world) {
    Transform local;

    // Each line undoes the matching one in transform_transform, in reverse
    local.scale = Vector3{
        parent_world.scale.x != 0.0f ? world.scale.x / parent_world.scale.x : 0.0f,
        parent_world.scale.y != 0.0f ? world.scale.y / parent_world.scale.y : 0.0f,
        parent_world.scale.z != 0.0f ? world.scale.z / parent_world.scale.z : 0.0f,
    };

    const Quaternion parent_inverse = QuaternionInvert(parent_world.rotation);
    local.rotation = QuaternionMultiply(parent_inverse, world.rotation);

    // Translate back, then turn back, then scale back: the reverse of the
    // order transform_transform applies them in
    const Vector3 moved = Vector3Subtract(world.translation, parent_world.translation);
    const Vector3 turned = Vector3RotateByQuaternion(moved, parent_inverse);
    local.translation = Vector3{
        parent_world.scale.x != 0.0f ? turned.x / parent_world.scale.x : 0.0f,
        parent_world.scale.y != 0.0f ? turned.y / parent_world.scale.y : 0.0f,
        parent_world.scale.z != 0.0f ? turned.z / parent_world.scale.z : 0.0f,
    };

    return local;
}

Matrix transform_to_matrix(Transform t) {
    Matrix scale = MatrixScale(t.scale.x, t.scale.y, t.scale.z);
    Matrix rotation = QuaternionToMatrix(t.rotation);
    Matrix translation = MatrixTranslate(t.translation.x, t.translation.y, t.translation.z);
    // Order: scale, then rotate, then translate
    return MatrixMultiply(MatrixMultiply(scale, rotation), translation);
}
void print_matrix(const Matrix& mat) {
    std::cout << "[\n";
    std::cout << "  " << mat.m0  << ", " << mat.m4  << ", " << mat.m8  << ", " << mat.m12 << "\n";
    std::cout << "  " << mat.m1  << ", " << mat.m5  << ", " << mat.m9  << ", " << mat.m13 << "\n";
    std::cout << "  " << mat.m2  << ", " << mat.m6  << ", " << mat.m10 << ", " << mat.m14 << "\n";
    std::cout << "  " << mat.m3  << ", " << mat.m7  << ", " << mat.m11 << ", " << mat.m15 << "\n";
    std::cout << "]\n";
}

