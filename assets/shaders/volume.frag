#version 330 core

// Two-light Blinn-Phong for the volumetric viewer. uUnlit = 1 renders
// flat vertex colors (slice planes, wireframe) at full alpha, always,
// whatever uShadingMode says — that branch is untouched by the material
// system below. uAlpha < 1 blends isosurfaces over the slice/cell furniture.
//
// uShadingMode picks the ISOSURFACE's own material — the same three-way
// vocabulary (and the same numbering) as isosurface.frag's SHADING_* for
// the main-viewport volumetric overlay, so the two describe one set of
// choices to the user, not two:
//   0 = Flat    : unshaded fill (the historical hardcoded look here)
//   1 = Diffuse : the two-light Lambert term below, ambient floor, no spec
//   2 = Glossy  : Diffuse plus a Blinn-Phong highlight
// uAmbient/uSpecular replace what used to be the literals 0.28 and 0.35;
// their defaults (set on the C++ side, see VolumeViewWidget::setIsoMaterial)
// reproduce the old hardcoded shader exactly, so a caller that never calls
// it sees no change.

#define SHADING_FLAT    0
#define SHADING_DIFFUSE 1
#define SHADING_GLOSSY  2

in vec3 vNormalView;
in vec3 vPosView;
in vec3 vColor;

uniform int uUnlit;
uniform float uAlpha;
uniform int uShadingMode;
uniform float uAmbient;
uniform float uSpecular;

out vec4 fragColor;

void main()
{
    if (uUnlit == 1) {
        fragColor = vec4(vColor, 1.0);
        return;
    }
    if (uShadingMode == SHADING_FLAT) {
        fragColor = vec4(vColor, uAlpha);
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
    vec3 color = vColor * (uAmbient + diffuse);
    if (uShadingMode == SHADING_GLOSSY) {
        vec3 h = normalize(keyDir + v);
        float spec = pow(max(dot(n, h), 0.0), 48.0) * uSpecular;
        color += vec3(spec);
    }
    fragColor = vec4(color, uAlpha);
}
