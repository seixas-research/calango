#version 330 core

// Impostor cylinder — vertex stage. One camera-facing quad per bond, on which
// the fragment stage intersects an analytic finite cylinder.
//
// A bond instance is built as
//   translate(from) · rotate(z -> direction) · scale(radius, radius, length)
// so the model matrix carries everything the intersection needs: column 3 is
// the base, column 2 is the axis scaled by the length, and column 0's length
// is the radius. The instance buffer is therefore unchanged from the
// tessellated path — the profile switch costs nothing in buildBuffers().

layout(location = 0) in vec3 aPos;     // unit quad corner, xy in [-1, 1]
layout(location = 1) in vec3 aNormal;  // unused
layout(location = 2) in mat4 iModel;
layout(location = 6) in vec4 iColor;   // colour at z = 0
layout(location = 7) in vec4 iColor2;  // colour at z = 1 (axial gradient)
layout(location = 8) in float iFinish;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightSpace;

out vec3 vPosView;
flat out vec3 vBaseView;   // cylinder base, view space
flat out vec3 vAxisView;   // base -> tip, view space (length = cylinder length)
flat out float vRadius;
flat out vec4 vColorA;
flat out vec4 vColorB;
flat out int vFinish;
flat out mat4 vLightSpace;

void main()
{
    vBaseView = (uView * vec4(iModel[3].xyz, 1.0)).xyz;
    vAxisView = mat3(uView) * iModel[2].xyz;
    vRadius = length(iModel[0].xyz);
    vColorA = iColor;
    vColorB = iColor2;
    vFinish = int(iFinish + 0.5);
    vLightSpace = uLightSpace;

    // Bounding quad: cover the whole segment plus a radius of margin at each
    // end, expanded in view space so it faces the camera. Built around the
    // segment's midpoint from its screen-space direction, so a bond seen
    // end-on still gets a quad wide enough to hold its cap.
    vec3 mid = vBaseView + vAxisView * 0.5;
    float halfLength = length(vAxisView) * 0.5;
    vec3 along = halfLength > 1e-6 ? normalize(vAxisView) : vec3(0.0, 0.0, 1.0);
    // Perpendicular to the axis AND to the view direction: the widest the
    // cylinder can appear on screen.
    vec3 side = cross(along, vec3(0.0, 0.0, 1.0));
    if (length(side) < 1e-4)
        side = cross(along, vec3(0.0, 1.0, 0.0)); // axis points at the camera
    side = normalize(side);
    // The margin is a full radius on every side: the cap of a cylinder seen
    // at a grazing angle reaches beyond the segment's own extent.
    vPosView = mid
             + along * aPos.y * (halfLength + vRadius)
             + side * aPos.x * vRadius;
    gl_Position = uProj * vec4(vPosView, 1.0);
}
