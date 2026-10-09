//
// Created by Claude on 09.10.2026.
//

#include "Frustum.hpp"

Frustum frustum_from_matrix(const Matrix m) {
    // raylib's Matrix is column major: a point is transformed as
    // x' = m0*x + m4*y + m8*z + m12, so the rows of the matrix as it is
    // applied are (m0, m4, m8, m12), (m1, m5, m9, m13) and so on.
    const Vector4 row0 = {m.m0, m.m4, m.m8, m.m12};
    const Vector4 row1 = {m.m1, m.m5, m.m9, m.m13};
    const Vector4 row2 = {m.m2, m.m6, m.m10, m.m14};
    const Vector4 row3 = {m.m3, m.m7, m.m11, m.m15};

    const auto add = [](const Vector4 a, const Vector4 b) {
        return Vector4{a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w};
    };
    const auto sub = [](const Vector4 a, const Vector4 b) {
        return Vector4{a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w};
    };

    // A clip space point is inside when -w <= x, y, z <= w. Each of those six
    // inequalities, written out against the rows, is one plane.
    Frustum frustum{};
    frustum.planes[0] = add(row3, row0);  // left:   w + x >= 0
    frustum.planes[1] = sub(row3, row0);  // right:  w - x >= 0
    frustum.planes[2] = add(row3, row1);  // bottom: w + y >= 0
    frustum.planes[3] = sub(row3, row1);  // top:    w - y >= 0
    frustum.planes[4] = add(row3, row2);  // near:   w + z >= 0
    frustum.planes[5] = sub(row3, row2);  // far:    w - z >= 0
    return frustum;
}

bool frustum_contains_box(const Frustum& frustum, const BoundingBox box) {
    for (const Vector4& plane : frustum.planes) {
        // The corner of the box furthest along the plane's normal. If even
        // that one is behind the plane, the whole box is.
        const Vector3 corner = {
            plane.x >= 0.0f ? box.max.x : box.min.x,
            plane.y >= 0.0f ? box.max.y : box.min.y,
            plane.z >= 0.0f ? box.max.z : box.min.z,
        };
        if (plane.x * corner.x + plane.y * corner.y + plane.z * corner.z + plane.w < 0.0f) {
            return false;
        }
    }
    return true;
}
