#version 330 core

// Direct volume rendering — vertex stage.
//
// Draws the volume's bounding box; the fragment stage marches a ray through
// the 3D texture between where it enters and leaves that box. Back faces are
// rendered (front faces culled) so the box still covers the screen when the
// camera is INSIDE the volume, which is the common case for a zoomed-in
// density.

layout(location = 0) in vec3 aPos;     // unit cube corner, [0,1]^3
layout(location = 1) in vec3 aNormal;  // unused
layout(location = 2) in vec3 aColor;   // unused

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uModel;   // unit cube -> the grid's own parallelepiped

out vec3 vTexCoord;    // position in [0,1]^3 texture space
out vec3 vPosView;

void main()
{
    // The unit cube's corner IS the texture coordinate: the model matrix maps
    // it onto the grid's origin + span vectors, so a non-orthogonal cell
    // (a triclinic crystal) is handled without any special case.
    vTexCoord = aPos;
    vec4 world = uModel * vec4(aPos, 1.0);
    vec4 posView = uView * world;
    vPosView = posView.xyz;
    gl_Position = uProj * posView;
}
