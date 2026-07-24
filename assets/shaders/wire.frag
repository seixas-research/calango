#version 330 core

in vec3 vColor;

// Opacity: 1.0 for wireframe bonds/atoms and polyhedra edges; < 1.0 for the
// translucent polyhedra faces.
uniform float uAlpha;

out vec4 fragColor;

void main()
{
    fragColor = vec4(vColor, uAlpha);
}
