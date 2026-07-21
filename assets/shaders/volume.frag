#version 330 core

// Two-light Blinn-Phong for the volumetric viewer. uUnlit = 1 renders
// flat vertex colors (slice planes, wireframe); uAlpha < 1 blends
// isosurfaces over the slice/cell furniture.

in vec3 vNormalView;
in vec3 vPosView;
in vec3 vColor;

uniform int uUnlit;
uniform float uAlpha;

out vec4 fragColor;

void main()
{
    if (uUnlit == 1) {
        fragColor = vec4(vColor, 1.0);
        return;
    }
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(-vPosView);

    vec3 keyDir = normalize(vec3(0.35, 0.45, 1.0));
    vec3 fillDir = normalize(vec3(-0.6, -0.2, 0.5));
    float diffuse = 0.75 * max(dot(n, keyDir), 0.0)
                  + 0.30 * max(dot(n, fillDir), 0.0);
    vec3 h = normalize(keyDir + v);
    float spec = pow(max(dot(n, h), 0.0), 48.0) * 0.35;
    vec3 color = vColor * (0.28 + diffuse) + vec3(spec);
    fragColor = vec4(color, uAlpha);
}
