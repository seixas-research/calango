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
in vec4 vPosLight;
flat in int vFinish;

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
// The finish arrives PER INSTANCE (vFinish, from the instance buffer) so casts
// can differ — there is no uniform for it.
// Which of the two mesh passes this is: 0 = opaque (glassy instances are
// discarded), 1 = the blended pass (everything else is discarded). Splitting
// them is what lets an opaque cast keep writing depth while a glassy one
// blends without it.
uniform int   uFinishPass;
uniform float uSurfaceOpacity;   // Glassy base alpha

// Directional shadow mapping (Visual Effects -> Shadow). The depth map is
// rendered from the primary light's point of view by shadow.vert; here each
// fragment re-projects into that light space and compares its depth against
// the stored one. uShadowStrength scales how dark an occluded fragment gets
// (0 = shadows off), and uShadowRadius is the PCF kernel half-width in
// texels: a wider kernel averages more neighbours and softens the edge.
uniform sampler2D uShadowMap;
uniform int   uShadowEnabled;
uniform float uShadowStrength;
uniform int   uShadowRadius;
uniform float uShadowTexelSize;

/// Fraction of the PCF kernel that is lit, in [0, 1].
float shadowVisibility(vec3 normal)
{
    if (uShadowEnabled == 0)
        return 1.0;
    // Perspective divide is a no-op for the orthographic light projection but
    // keeps this correct if the light ever becomes a spot light.
    vec3 proj = vPosLight.xyz / vPosLight.w;
    proj = proj * 0.5 + 0.5;                 // NDC [-1,1] -> texture [0,1]
    if (proj.z > 1.0)
        return 1.0;                          // beyond the light's far plane
    // Outside the shadow frustum there is no occlusion information; treating
    // those fragments as lit avoids a hard dark band at the map's edge.
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

    // Slope-scaled bias: surfaces nearly edge-on to the light need a larger
    // offset or they self-shadow into acne stripes.
    vec3 lightDir = normalize(-uLightDir[0]);
    float cosTheta = clamp(dot(normalize(normal), lightDir), 0.0, 1.0);
    float bias = max(0.0035 * (1.0 - cosTheta), 0.0008);

    int radius = clamp(uShadowRadius, 0, 6);
    float lit = 0.0;
    float samples = 0.0;
    for (int x = -radius; x <= radius; ++x) {
        for (int y = -radius; y <= radius; ++y) {
            float stored = texture(uShadowMap,
                                   proj.xy + vec2(x, y) * uShadowTexelSize).r;
            lit += (proj.z - bias) > stored ? 0.0 : 1.0;
            samples += 1.0;
        }
    }
    return samples > 0.0 ? lit / samples : 1.0;
}

// Distance fog (View -> Visual Effects): 0 = off, 1 = linear between
// uFogStart/uFogEnd, 2 = exponential with uFogDensity. uFogColor tracks
// the viewport background so faded geometry blends into it.
uniform int   uFogMode;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

layout(location = 0) out vec4 fragColor;
// G-buffer attachment 1: view-space normal for the SSAO pass, encoded into
// [0,1] (rgb = n * 0.5 + 0.5). Alpha is a validity flag — SSAO skips
// fragments that carry no meaningful normal. Discarded when the target has a
// single draw buffer (SSAO off), so this costs nothing in the plain path.
layout(location = 1) out vec4 gNormal;

void main()
{
    int finish = vFinish;
    bool glassy = finish == FINISH_GLASSY;
    // An instance is translucent if its material says so OR if its cast was
    // given an opacity below 1 (which arrives in the colour alpha). Both kinds
    // belong to the blended pass, so they are split off together.
    bool translucent = glassy || vColor.a < 0.999;
    // Each pass draws only the instances it owns.
    if ((uFinishPass == 0 && translucent) || (uFinishPass == 1 && !translucent))
        discard;

    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(-vPosView);

    // Per-material weights, resolved once outside the light loop.
    float specularWeight = 1.0;
    float diffuseWeight  = 1.0;
    float shininess      = uShininess;
    if (finish == FINISH_SHINY) {
        specularWeight = 2.2;
        diffuseWeight  = 0.9;   // let the highlight carry the form
        shininess      = uShininess * 4.0; // small, crisp highlight
    } else if (finish == FINISH_MATTE) {
        specularWeight = 0.0;   // fosco: purely diffuse
        diffuseWeight  = 1.15;  // compensate the lost highlight energy
    } else if (glassy) {
        specularWeight = 1.8;
        diffuseWeight  = 0.75;  // let the body read as translucent
        shininess      = uShininess * 2.5; // tighter, glassier highlight
    }

    // Only the primary light (index 0) casts shadows — it is the one the
    // depth map was rendered from. Fill lights stay unshadowed, which is also
    // what keeps shadowed regions readable rather than black.
    float visibility = shadowVisibility(n);
    float primaryFactor = mix(1.0, visibility, clamp(uShadowStrength, 0.0, 1.0));

    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), shininess) : 0.0;
        // Ambient is never shadowed: it stands in for bounced light, and
        // attenuating it would drive occluded geometry to pure black.
        float direct = (i == 0) ? primaryFactor : 1.0;
        color += vColor.rgb * uLightAmbient[i]
               + vColor.rgb * uLightDiffuse[i] * ndl * diffuseWeight * direct
               + uLightSpecular[i] * spec * specularWeight * direct;
    }

    // vColor.a carries the cast's own opacity slider.
    float alpha = vColor.a;
    if (glassy) {
        // Schlick-style Fresnel: face-on is the most transparent, edges
        // approach opaque. This rim is what sells the glass read, and it also
        // keeps silhouettes legible when spheres overlap.
        float fresnel = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);
        float glass = clamp(uSurfaceOpacity + (1.0 - uSurfaceOpacity) * fresnel,
                            0.0, 1.0);
        // The two compose rather than one replacing the other: a glassy cast
        // faded to 0.3 must end up fainter than an opaque glassy one, not
        // identical to it.
        alpha = glass * vColor.a;
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
    // `n` is the shading normal, already flipped to face the viewer, which is
    // exactly what the hemisphere sampling wants.
    gNormal = vec4(normalize(n) * 0.5 + 0.5, 1.0);
}
