#version 330 core

// Lit volumetric-isosurface pass.
//
// Replaces the flat per-vertex-colour path the viewport used to draw
// isosurfaces through (wire.vert/frag). That path had no normals at all, so
// the shading had to be baked into the vertex colours on the CPU — which
// froze the highlight to a fixed direction and left the surface out of the
// SSAO G-buffer entirely.
//
// The vertex format therefore grows a normal channel. Marching cubes already
// derives one per vertex from the field gradient (core::IsoMesh::normals), so
// nothing new has to be computed to feed this.

// Location 1 is the COLOUR and location 2 the normal, which is not the order
// they sit in memory. It matches wire.vert, the legacy profile that draws out
// of this same buffer and declares 0 = position, 1 = colour — so one VAO
// serves both programs. See createLitBuffer() for the full reasoning.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec3 aNormal;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormalView;
out vec3 vPosView;
out vec3 vColor;

void main()
{
    vec4 posView = uView * vec4(aPos, 1.0);
    vPosView = posView.xyz;
    // The overlay is placed in world space with no per-object transform, so
    // the view rotation alone carries the normal — no inverse-transpose is
    // needed here (unlike mesh.vert, which has to undo the bond cylinders'
    // non-uniform axis scaling).
    vNormalView = mat3(uView) * aNormal;
    vColor = aColor;
    gl_Position = uProj * posView;
}
