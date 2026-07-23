#version 330 core

// Depth-only pass: renders the same instanced meshes (atom spheres, bond
// cylinders, cell tubes) from the primary light's point of view. Only
// gl_Position matters — the fragment stage writes nothing but depth — so the
// normal and color attributes are declared to keep the vertex layout
// identical to mesh.vert (the same VAOs are bound for both passes) but are
// left unused.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 iModel;   // per-instance, occupies locations 2..5
layout(location = 6) in vec4 iColor;
layout(location = 7) in vec4 iColor2;

// World -> light clip space (orthographic, fitted to the scene bounds).
uniform mat4 uLightSpace;

void main()
{
    gl_Position = uLightSpace * iModel * vec4(aPos, 1.0);
}
