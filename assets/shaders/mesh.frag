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

// Surface finish (Representation panel). One program still shades every
// instanced mesh; the material only rescales the Blinn-Phong terms and the
// output alpha, so atoms, bonds and cell tubes stay consistent.
//   0 = Standard : plain Blinn-Phong, opaque.
//   1 = Shiny    : polished metal/ceramic — stronger specular and a much
//                  higher exponent, so highlights are small and crisp
//                  (the shading equivalent of low surface roughness).
//   2 = Matte    : specular suppressed, slightly lifted diffuse so the
//                  surface does not just look darker than Standard.
//   3 = Glassy   : semi-transparent with a stronger, tighter highlight and a
//                  Fresnel rim — grazing angles turn opaque, which is what
//                  reads as "glass" without real refraction (a single
//                  forward pass has no scene texture to refract).
#define FINISH_STANDARD 0
#define FINISH_SHINY    1
#define FINISH_MATTE    2
#define FINISH_GLASSY   3
uniform int   uSurfaceFinish;
uniform float uSurfaceOpacity;   // Glassy base alpha

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

    // Per-material weights, resolved once outside the light loop.
    float specularWeight = 1.0;
    float diffuseWeight  = 1.0;
    float shininess      = uShininess;
    if (uSurfaceFinish == FINISH_SHINY) {
        specularWeight = 2.2;
        diffuseWeight  = 0.9;   // let the highlight carry the form
        shininess      = uShininess * 4.0; // small, crisp highlight
    } else if (uSurfaceFinish == FINISH_MATTE) {
        specularWeight = 0.0;   // fosco: purely diffuse
        diffuseWeight  = 1.15;  // compensate the lost highlight energy
    } else if (uSurfaceFinish == FINISH_GLASSY) {
        specularWeight = 1.8;
        diffuseWeight  = 0.75;  // let the body read as translucent
        shininess      = uShininess * 2.5; // tighter, glassier highlight
    }

    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), shininess) : 0.0;
        color += vColor.rgb * uLightAmbient[i]
               + vColor.rgb * uLightDiffuse[i] * ndl * diffuseWeight
               + uLightSpecular[i] * spec * specularWeight;
    }

    float alpha = vColor.a;
    if (uSurfaceFinish == FINISH_GLASSY) {
        // Schlick-style Fresnel: face-on is the most transparent, edges
        // approach opaque. This rim is what sells the glass read, and it also
        // keeps silhouettes legible when spheres overlap.
        float fresnel = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);
        alpha = clamp(uSurfaceOpacity + (1.0 - uSurfaceOpacity) * fresnel, 0.0, 1.0);
        color += uLightSpecular[0] * fresnel * 0.25; // subtle edge sheen
    }
    if (uFogMode != 0) {
        float dist = length(vPosView);
        float visibility = uFogMode == 1
            ? clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0)
            : exp(-uFogDensity * max(dist - uFogStart, 0.0));
        color = mix(uFogColor, color, visibility);
    }
    fragColor = vec4(color, alpha);
}
