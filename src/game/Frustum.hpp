//
// Created by Claude on 09.10.2026.
//

#ifndef BUSINESS_GAME_FRUSTUM_HPP
#define BUSINESS_GAME_FRUSTUM_HPP
#include <raylib.h>

/**
 * The six planes of what a camera can see, for throwing away whatever is
 * entirely outside it before it is drawn.
 *
 * Plain maths with no OpenGL in it, so the tests can cover it. Inside a
 * BeginMode3D() block, rlGetMatrixModelview() and rlGetMatrixProjection() are
 * the two matrices to build one from, which keeps it in step with whatever
 * BeginMode3D() actually set up (field of view, aspect, near and far).
 */
struct Frustum {
    // Each plane is (a, b, c, d): a point p is on the inside when
    // a*p.x + b*p.y + c*p.z + d >= 0. Not normalised, as the box test below
    // only cares about the sign.
    // Left, right, bottom, top, near, far.
    Vector4 planes[6];
};

/**
 * The frustum of a view-projection matrix, in raylib's order:
 * MatrixMultiply(view, projection), the same way rlgl builds its mvp.
 * Gribb and Hartmann's extraction, for OpenGL's -w..w clip space.
 */
Frustum frustum_from_matrix(Matrix view_projection);

/**
 * Whether any of the box could be on screen. Conservative: a box that is
 * outside but straddles the extension of two planes (near a corner of the
 * frustum) is still called visible, which only ever costs a wasted draw.
 */
bool frustum_contains_box(const Frustum& frustum, BoundingBox box);

#endif //BUSINESS_GAME_FRUSTUM_HPP
