#version 330 core

uniform vec4 uColor;

layout(location = 0) out vec4 fragColor;
// G-buffer attachment 1 (SSAO): lines carry no surface normal, so alpha = 0
// marks the fragment as "no ambient occlusion here". Leaving it unwritten
// would hand the SSAO pass undefined memory.
layout(location = 1) out vec4 gNormal;

void main()
{
    fragColor = uColor;
    gNormal = vec4(0.0);
}
