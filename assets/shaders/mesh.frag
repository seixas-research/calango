#version 330 core

// Blinn-Phong with up to 4 independent directional lights (key, fill, ...).
// Light directions are in VIEW space (camera-relative), so lighting stays
// fixed relative to the viewer while the camera orbits the structure.
//
// This single program shades EVERY instanced mesh — atomic spheres, bond
// cylinders and cell tubes — so ambient, diffuse and specular terms are
// evaluated identically on all of them from their per-vertex surface
// normals (cylinders carry radial normals; mesh.vert's inverse-transpose
// keeps them correct under the cylinders' non-uniform axis scaling).

#define MAX_LIGHTS 4

in vec3 vNormalView;
in vec3 vPosView;
in vec4 vColor;

uniform int   uLightCount;
uniform vec3  uLightDir[MAX_LIGHTS];      // direction the light travels
uniform vec3  uLightAmbient[MAX_LIGHTS];
uniform vec3  uLightDiffuse[MAX_LIGHTS];
uniform vec3  uLightSpecular[MAX_LIGHTS];
uniform float uShininess;

// Distance fog (View -> Visual Effects): 0 = off, 1 = linear between
// uFogStart/uFogEnd, 2 = exponential with uFogDensity. uFogColor tracks
// the viewport background so faded geometry blends into it.
uniform int   uFogMode;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

out vec4 fragColor;

void main()
{
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(-vPosView);

    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), uShininess) : 0.0;
        color += vColor.rgb * uLightAmbient[i]
               + vColor.rgb * uLightDiffuse[i] * ndl
               + uLightSpecular[i] * spec;
    }
    if (uFogMode != 0) {
        float dist = length(vPosView);
        float visibility = uFogMode == 1
            ? clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0)
            : exp(-uFogDensity * max(dist - uFogStart, 0.0));
        color = mix(uFogColor, color, visibility);
    }
    fragColor = vec4(color, vColor.a);
}
