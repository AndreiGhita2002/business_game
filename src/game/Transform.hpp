//
// Created by Andrei Ghita on 29.08.2026.
//

#ifndef BUSINESS_GAME_TRANSFORM_HPP
#define BUSINESS_GAME_TRANSFORM_HPP
#include <raylib.h>

/**
 * Transform maths, on its own so that anything composing transforms can have it
 * without pulling in the window, the shaders and the view tree that main.hpp
 * carries. Nothing in here touches the GPU or any global state, which is also
 * what makes it testable without a window.
 *
 * A Transform is applied in the order scale, rotate, translate, the same order
 * DrawModelEx uses.
 */

void apply_transform(Vector3* position, Quaternion* rotation, Vector3* scale, const Transform& t);

/** A point moved by a transform: scaled, then rotated, then translated. */
Vector3 apply_transform_trans(Vector3 v, const Transform &t);

Quaternion apply_transform_rot(Quaternion rot, const Transform &t);

Vector3 apply_transform_scale(Vector3 scale, const Transform &t);

/**
 * `base` with `applied` on top of it, so the transform a child ends up with
 * when its parent is moved. `base` happens first.
 */
Transform transform_transform(const Transform& base, const Transform& applied);

/**
 * The other way round: the local transform a grid needs to stand at `world`
 * while hanging off a parent that is at `parent_world`. Undoes
 * transform_transform, so transform_transform(transform_relative_to(w, p), p)
 * is w again.
 */
Transform transform_relative_to(const Transform& world, const Transform& parent_world);

/** The matrix form of a transform: scale, then rotate, then translate. */
Matrix transform_to_matrix(Transform t);

void print_matrix(const Matrix& mat);

#endif //BUSINESS_GAME_TRANSFORM_HPP
