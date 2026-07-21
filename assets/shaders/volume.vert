#version 330 core

// Plain (non-instanced) lit-mesh shader for the volumetric viewer:
// isosurfaces, slice planes and the cell wireframe share it.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aColor;

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormalView;
out vec3 vPosView;
out vec3 vColor;

void main()
{
    vec4 posView = uView * vec4(aPos, 1.0);
    vPosView = posView.xyz;
    vNormalView = mat3(uView) * aNormal;
    vColor = aColor;
    gl_Position = uProj * posView;
}
