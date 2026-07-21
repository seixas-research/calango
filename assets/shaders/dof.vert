#version 330 core

// Fullscreen triangle from gl_VertexID — no vertex buffer needed.

out vec2 vUv;

void main()
{
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
