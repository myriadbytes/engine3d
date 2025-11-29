// TODO: It could be interesting to not even include math.h, and instead
// roll our own math functions. It depends on whether or not this would
// require too much math knowledge or if it's at least somewhat possible to
// learn in a couple of days.
#include "maths.h"

m4 lookAt(v3 eye, v3 target) {
    v3 up = {0, 1, 0};

    // NOTE: Assumes a RH coordinate system, where things in front of the camera are projected in the -Z region.
    // This is why we are doing (eye - target) for the camera z axis.
    v3 z = normalize(eye - target);
    v3 x = normalize(cross(up, z));
    v3 y = normalize(cross(z, x));

    // NOTE: The view matrix is the the inverse (transpose since it's orthogonal) of the matrix with camera axes as columns.
    // So the final matrix has camera axes as rows, keeping in mind that graphics API matrices are stored column-major.
    m4 rotation_part = makeMatrixFromRows(x, y, z);

    // NOTE: So far we are rotating the world correctly, but we are missing the translation part which is pretty straightforward.
    m4 translation_part = makeTranslation(-eye.x(), -eye.y(), -eye.z());

    // NOTE: The order of transformation is reversed because the matrices themselves are already inverted. Tricky.
    // https://www.3dgep.com/understanding-the-view-matrix/
    m4 result = rotation_part * translation_part;

    return result;
}

m4 makeProjection(f32 near, f32 far, f32 fov, f32 aspect) {

    f32 f = 1.f / (
        tanf((fov/2) * (PI32 / 180))
    );

    // WARNING: The y component is negated to account for the fact that Vulkan's
    // NDC is +Y down. Maybe the right approach is to flip Vulkan's viewport ?
    // Or at least make the flipping a parameter of this function.
    f32 fx = f / aspect;
    f32 fy = -f;

    return m4 {
        fx, 0, 0, 0,
        0, fy, 0, 0,
        0, 0, -(far / (far - near)), -1,
        0, 0, -((far * near) / (far - near)), 0
    };
}
