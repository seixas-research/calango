#version 330 core

// Ground plane, shaded exactly like every other lit surface in the scene:
// the same Blinn-Phong terms as mesh.frag, the same view-space lights, the
// same percentage-closer shadow lookup and the same distance fog. It is a
// SEPARATE program only because it needs two things no instanced mesh does —
// a radial alpha fade toward the horizon, and single-sidedness.
//
// The shadow function below is a copy of mesh.frag's. GLSL has no #include,
// and impostor_sphere.frag already carries the same copy; keeping the three in
// step by hand is the established cost of the shared lookup here.

#define MAX_LIGHTS 4

in vec3 vPosView;
in vec3 vNormalView;
in vec4 vPosLight;
in vec2 vOffsetUV;

uniform int   uLightCount;
uniform vec3  uLightDir[MAX_LIGHTS];      // direction the light travels
uniform vec3  uLightAmbient[MAX_LIGHTS];
uniform vec3  uLightDiffuse[MAX_LIGHTS];
uniform vec3  uLightSpecular[MAX_LIGHTS];
uniform float uShininess;

// Surface finish, sharing render::SurfaceFinish with the atoms and bonds so
// "Matte" means the same thing on the floor as it does on a sphere.
#define FINISH_STANDARD 0
#define FINISH_SHINY    1
#define FINISH_MATTE    2
#define FINISH_GLASSY   3
uniform int   uFloorFinish;
uniform vec3  uFloorColor;
uniform float uFloorOpacity;

// Radial fade. Inside uSolidRadius the plane is fully opaque; past uFadeRadius
// it is gone. That gradient is what makes a finite quad read as an infinite
// ground: there is no edge to see, only a horizon, and because it fades to
// ALPHA rather than to a colour it composites correctly over a transparent
// background as well as over the viewport's own.
uniform float uSolidRadius;
uniform float uFadeRadius;

// Directional shadow mapping — see mesh.frag. The floor's whole purpose is to
// receive these.
uniform sampler2D uShadowMap;
uniform int   uShadowEnabled;
uniform float uShadowStrength;
uniform int   uShadowRadius;
uniform float uShadowTexelSize;

uniform int   uFogMode;
uniform vec3  uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uFogDensity;

layout(location = 0) out vec4 fragColor;
// G-buffer attachment 1: view-space normal for SSAO, encoded into [0,1].
// Writing it is what gives the contact region between the structure and the
// floor its ambient-occlusion darkening.
layout(location = 1) out vec4 gNormal;

/// Fraction of the PCF kernel that is lit, in [0, 1].
float shadowVisibility(vec3 normal)
{
    if (uShadowEnabled == 0)
        return 1.0;
    vec3 proj = vPosLight.xyz / vPosLight.w;
    proj = proj * 0.5 + 0.5;                 // NDC [-1,1] -> texture [0,1]
    if (proj.z > 1.0)
        return 1.0;                          // beyond the light's far plane
    // Outside the fitted light frustum there is no occlusion information. The
    // floor reaches far past that frustum by design, so this branch is the
    // common case out near the horizon — returning "lit" is what keeps the
    // plane from developing a hard square of darkness at the map's edge.
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;

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
    if (samples <= 0.0)
        return 1.0;

    // Feather the outermost band of the map back to "fully lit".
    //
    // This is the one thing the floor needs that no instanced mesh does. Every
    // other surface in the scene lives INSIDE the fitted light frustum, so its
    // boundary is never reached; the plane reaches an order of magnitude
    // further, crosses that boundary in mid-surface, and printed it as a faint
    // rectangle drawn across the ground — a light frustum made visible, which
    // in a figure reads as a rendering error. A smooth ramp has no edge to
    // print. Nothing is lost: the structure's own shadow lands near the middle
    // of the map, nowhere near this band.
    vec2 inset = min(proj.xy, 1.0 - proj.xy);
    float edge = clamp(min(inset.x, inset.y) / 0.03, 0.0, 1.0);
    return mix(1.0, lit / samples, edge);
}

void main()
{
    // Single-sided: seen from below, the floor is not drawn at all.
    //
    // An opaque plate under the structure is physically the honest answer, but
    // orbiting under a molecule and losing the whole scene behind a grey slab
    // is not what anyone wants from a display aid — and because the plane
    // writes depth (SSAO and depth-of-field both read it), an unculled floor
    // would occlude rather than merely cover. Discarding the back face keeps
    // the structure visible from every angle.
    if (!gl_FrontFacing)
        discard;

    float radius = length(vOffsetUV);
    float fade = 1.0 - smoothstep(uSolidRadius, uFadeRadius, radius);
    float alpha = fade * clamp(uFloorOpacity, 0.0, 1.0);
    // Nothing left to blend, and the fragment would still write depth and
    // occlude whatever lies beyond the horizon.
    if (alpha < 0.004)
        discard;

    vec3 n = normalize(vNormalView);
    vec3 v = normalize(-vPosView);

    float specularWeight = 1.0;
    float diffuseWeight  = 1.0;
    float shininess      = uShininess;
    if (uFloorFinish == FINISH_SHINY) {
        specularWeight = 2.2;
        diffuseWeight  = 0.9;
        shininess      = uShininess * 4.0;
    } else if (uFloorFinish == FINISH_MATTE) {
        specularWeight = 0.0;   // fosco: purely diffuse
        diffuseWeight  = 1.15;
    } else if (uFloorFinish == FINISH_GLASSY) {
        specularWeight = 1.8;
        diffuseWeight  = 0.75;
        shininess      = uShininess * 2.5;
    }

    // Only light 0 casts, exactly as in mesh.frag — the depth map was rendered
    // from it, and the fill lights staying unshadowed is what keeps the shadow
    // on the floor readable rather than a black hole.
    float visibility = shadowVisibility(n);
    float primaryFactor = mix(1.0, visibility, clamp(uShadowStrength, 0.0, 1.0));

    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), shininess) : 0.0;
        float direct = (i == 0) ? primaryFactor : 1.0;
        color += uFloorColor * uLightAmbient[i]
               + uFloorColor * uLightDiffuse[i] * ndl * diffuseWeight * direct
               + uLightSpecular[i] * spec * specularWeight * direct;
    }

    if (uFogMode != 0) {
        float dist = length(vPosView);
        float fogVisibility = uFogMode == 1
            ? clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0)
            : exp(-uFogDensity * max(dist - uFogStart, 0.0));
        color = mix(uFogColor, color, fogVisibility);
    }

    fragColor = vec4(color, alpha);
    gNormal = vec4(normalize(n) * 0.5 + 0.5, 1.0);
}
