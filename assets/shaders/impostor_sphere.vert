#version 330 core

// Impostor sphere — vertex stage.
//
// Instead of a 1200-triangle tessellated sphere per atom, ONE screen-facing
// quad per atom, on which the fragment stage intersects a real analytic
// sphere. The silhouette is then exact at every zoom (no facets appear when a
// sphere fills the screen) and the vertex cost drops from 651 vertices per
// atom to 4.
//
// The instance buffer is completely unchanged: the same 25-float record the
// tessellated path uses. A sphere instance is built as
// `model.translate(pos); model.scale(radius);`, so the centre is the
// translation column and the radius is the length of any basis column — which
// means switching profiles needs no change whatsoever in buildBuffers().

layout(location = 0) in vec3 aPos;     // unit quad corner, xy in [-1, 1]
layout(location = 1) in vec3 aNormal;  // unused; keeps the layout shared
layout(location = 2) in mat4 iModel;   // per-instance, locations 2..5
layout(location = 6) in vec4 iColor;
layout(location = 7) in vec4 iColor2;
layout(location = 8) in float iFinish;

uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightSpace;

out vec3 vPosView;          // the point on the quad this fragment covers
flat out vec3 vCenterView;  // sphere centre, view space
flat out float vRadius;
flat out vec4 vColor;
flat out int vFinish;
flat out mat4 vLightSpace;

void main()
{
    vCenterView = (uView * vec4(iModel[3].xyz, 1.0)).xyz;
    // Uniform scale for spheres, so any basis column gives the radius.
    vRadius = length(iModel[0].xyz);

    // Colour is uniform over a sphere (the axial gradient only means anything
    // on a bond), so the z = 0 end is the whole story.
    vColor = iColor;
    vFinish = int(iFinish + 0.5);
    vLightSpace = uLightSpace;

    // Expand the quad in VIEW space, so it always faces the camera. The 1.5
    // margin covers the perspective foreshortening that makes a sphere project
    // to an ellipse slightly LARGER than its radius: too tight a quad clips
    // the silhouette of spheres near the frame edge, which shows as atoms with
    // a slice missing.
    const float kMargin = 1.5;
    vPosView = vCenterView + vec3(aPos.xy * vRadius * kMargin, 0.0);
    gl_Position = uProj * vec4(vPosView, 1.0);
}
