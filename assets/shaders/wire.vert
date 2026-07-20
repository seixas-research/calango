#version 330 core

// Wireframe representation: bond lines and atom points with per-vertex color.

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;

uniform mat4 uMvp;

out vec3 vColor;

void main()
{
    vColor = aColor;
    gl_Position = uMvp * vec4(aPos, 1.0);
    gl_PointSize = 7.0; // isolated atoms drawn as points
}
