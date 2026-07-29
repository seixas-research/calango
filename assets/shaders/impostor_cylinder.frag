#version 330 core

// Impostor cylinder — fragment stage. Ray-traces a finite capped cylinder and
// writes true depth.
//
// The lighting is DUPLICATED from mesh.frag for the reason set out at the top
// of impostor_sphere.frag: mesh.frag is the profile that must stay
// bit-identical, and the pixel test cannot underwrite a shared refactor. Any
// change to the light loop, the finishes, the shadow lookup or the fog must be
// made in mesh.frag, impostor_sphere.frag AND here.

#define MAX_LIGHTS 4

in vec3 vPosView;
flat in vec3 vBaseView;
flat in vec3 vAxisView;
flat in float vRadius;
flat in vec4 vColorA;
flat in vec4 vColorB;
flat in int vFinish;
flat in mat4 vLightSpace;

uniform mat4 uProj;
uniform mat4 uInvView;

uniform int   uLightCount;
uniform vec3  uLightDir[MAX_LIGHTS];
uniform vec3  uLightAmbient[MAX_LIGHTS];
uniform vec3  uLightDiffuse[MAX_LIGHTS];
uniform vec3  uLightSpecular[MAX_LIGHTS];
uniform float uShininess;

#define FINISH_STANDARD 0
#define FINISH_SHINY    1
#define FINISH_MATTE    2
#define FINISH_GLASSY   3
uniform int   uFinishPass;
uniform float uSurfaceOpacity;

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

// ---------------------------------------------------------------------------
// Shading models. Selected by uShadingModel, which is a uniform rather than a
// separate program: the ray/primitive intersection above is the delicate part
// and is identical for all three, so duplicating it to vary the BRDF would
// triple the surface area of the code most likely to be got wrong.
// ---------------------------------------------------------------------------
#define SHADING_BLINN 0
#define SHADING_PBR   1
#define SHADING_TOON  2
uniform int   uShadingModel;
uniform float uMetallic;    // 0 = dielectric, 1 = metal
uniform float uRoughness;   // GGX alpha = roughness^2
uniform int   uToonBands;   // quantization steps for the toon model
uniform float uToonRim;     // rim-darkening width, 0 = off

const float kPi = 3.14159265359;

/// Analytic environment irradiance for a direction.
///
/// Stands in for an image-based light. A real IBL needs a prefiltered cubemap,
/// which is a binary ASSET rather than code — a bigger and less reversible
/// commitment than a shader — so the environment is approximated here as a
/// three-zone studio gradient: cool sky above, neutral horizon, warm bounce
/// below. It is what gives a metal something to reflect; without any
/// environment term a metallic surface renders as flat grey and PBR looks
/// worse than Blinn-Phong, not better.
///
/// Swap this function for a cubemap sample and the rest of the PBR path is
/// unchanged.
vec3 environment(vec3 dir)
{
    float up = dir.y * 0.5 + 0.5;
    vec3 sky = vec3(0.42, 0.49, 0.62);
    vec3 horizon = vec3(0.34, 0.34, 0.36);
    vec3 ground = vec3(0.20, 0.17, 0.14);
    return up > 0.5 ? mix(horizon, sky, (up - 0.5) * 2.0)
                    : mix(ground, horizon, up * 2.0);
}

/// GGX / Trowbridge-Reitz normal distribution.
float distributionGGX(float ndh, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = ndh * ndh * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-7);
}

/// Smith geometry term with the Schlick-GGX approximation, direct-light k.
float geometrySmith(float ndv, float ndl, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = ndv / (ndv * (1.0 - k) + k);
    float gl = ndl / (ndl * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 f0)
{
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/// Cook-Torrance direct lighting plus an ambient environment term.
vec3 shadePbr(vec3 n, vec3 v, vec3 albedo, float primaryFactor)
{
    float roughness = clamp(uRoughness, 0.045, 1.0); // 0 makes the NDF explode
    float metallic = clamp(uMetallic, 0.0, 1.0);
    // Dielectrics reflect ~4 % at normal incidence; metals tint the specular
    // with their own colour and have no diffuse lobe at all. That single fact
    // is what makes a metal look like metal rather than like grey plastic.
    vec3 f0 = mix(vec3(0.04), albedo, metallic);
    float ndv = max(dot(n, v), 1e-4);

    vec3 direct = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        if (ndl <= 0.0)
            continue;
        float ndh = max(dot(n, h), 0.0);
        float d = distributionGGX(ndh, roughness);
        float g = geometrySmith(ndv, ndl, roughness);
        vec3 f = fresnelSchlick(max(dot(h, v), 0.0), f0);
        vec3 specular = (d * g * f) / max(4.0 * ndv * ndl, 1e-7);
        // Energy conservation: what is not reflected specularly is available
        // to the diffuse lobe, and metals keep none of it.
        vec3 kd = (vec3(1.0) - f) * (1.0 - metallic);
        float shadowed = (i == 0) ? primaryFactor : 1.0;
        direct += (kd * albedo / kPi + specular)
                * uLightDiffuse[i] * ndl * shadowed;
    }

    // Ambient from the analytic environment: a diffuse term from the normal
    // and a specular term from the reflection, weighted by Fresnel so grazing
    // angles brighten. Roughness blends the reflection toward the diffuse
    // direction, which is a cheap stand-in for prefiltered mip selection.
    vec3 r = reflect(-v, n);
    vec3 irradiance = environment(n);
    vec3 reflected = environment(normalize(mix(r, n, roughness)));
    vec3 f = fresnelSchlick(ndv, f0);
    vec3 kd = (vec3(1.0) - f) * (1.0 - metallic);
    vec3 ambient = kd * albedo * irradiance + reflected * f;
    return direct + ambient;
}

/// Quantized diffuse with a darkened rim — the stylized/figure look.
vec3 shadeToon(vec3 n, vec3 v, vec3 albedo, float primaryFactor)
{
    float bands = float(max(uToonBands, 1));
    float lit = 0.0;
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        float ndl = max(dot(n, l), 0.0);
        lit += ndl * ((i == 0) ? primaryFactor : 1.0)
             * (i == 0 ? 1.0 : 0.35);
    }
    // Quantize AFTER summing the lights: banding each one separately produces
    // interference between the two step patterns, which reads as noise rather
    // than as cel shading.
    float quantized = floor(clamp(lit, 0.0, 1.0) * bands + 0.5) / bands;
    vec3 color = albedo * (0.25 + 0.75 * quantized);

    // Silhouette darkening. On spheres and cylinders — which is everything
    // this shader draws — the rim IS the outline, so no screen-space edge pass
    // is needed to get the characteristic ink border.
    if (uToonRim > 0.0) {
        float rim = 1.0 - clamp(dot(n, v), 0.0, 1.0);
        float edge = smoothstep(1.0 - uToonRim, 1.0, rim);
        color = mix(color, vec3(0.05), edge);
    }
    return color;
}

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gNormal;

float shadowVisibility(vec3 normal, vec4 posLight)
{
    if (uShadowEnabled == 0)
        return 1.0;
    vec3 proj = posLight.xyz / posLight.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0)
        return 1.0;
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
    return samples > 0.0 ? lit / samples : 1.0;
}

void main()
{
    int finish = vFinish;
    bool glassy = finish == FINISH_GLASSY;
    // The axial gradient means the two ends can differ in alpha; the pass
    // split has to use one value, and the base end is the one the tessellated
    // path keys on too.
    bool translucent = glassy || vColorA.a < 0.999;
    if ((uFinishPass == 0 && translucent) || (uFinishPass == 1 && !translucent))
        discard;

    // --- Ray/finite-cylinder intersection ----------------------------------
    // Eye at the origin in view space. Solve for the infinite cylinder about
    // the axis, then clamp to the segment and test the two caps, keeping
    // whichever valid hit is nearest.
    vec3 rd = normalize(vPosView);
    float height = length(vAxisView);
    if (height < 1e-6)
        discard;
    vec3 axis = vAxisView / height;

    vec3 oc = -vBaseView;                 // base -> eye
    // Components perpendicular to the axis: the 2D circle problem.
    vec3 rdPerp = rd - axis * dot(rd, axis);
    vec3 ocPerp = oc - axis * dot(oc, axis);
    float a = dot(rdPerp, rdPerp);
    float b = dot(rdPerp, ocPerp);
    float c = dot(ocPerp, ocPerp) - vRadius * vRadius;

    float best = 1e30;
    vec3 bestNormal = vec3(0.0);
    bool hitFound = false;

    if (a > 1e-12) {
        float disc = b * b - a * c;
        if (disc >= 0.0) {
            float sq = sqrt(disc);
            // Both roots: the near one is the outer wall, the far one the
            // inside of the tube, which is what shows when the camera sits
            // inside a fat bond.
            for (int k = 0; k < 2; ++k) {
                float t = (k == 0) ? (-b - sq) / a : (-b + sq) / a;
                if (t < 0.0 || t >= best)
                    continue;
                vec3 p = rd * t;
                float along = dot(p - vBaseView, axis);
                if (along < 0.0 || along > height)
                    continue;           // beyond the segment: a cap may still hit
                best = t;
                bestNormal = normalize(p - (vBaseView + axis * along));
                hitFound = true;
                break;
            }
        }
    }

    // Caps. Without them a bond viewed along its axis is a hole, and the
    // licorice representation — which is nothing but tube ends — falls apart.
    float denom = dot(rd, axis);
    if (abs(denom) > 1e-9) {
        for (int k = 0; k < 2; ++k) {
            vec3 capCenter = vBaseView + axis * (k == 0 ? 0.0 : height);
            float t = dot(capCenter, axis) / denom;
            if (t < 0.0 || t >= best)
                continue;
            vec3 p = rd * t;
            if (dot(p - capCenter, p - capCenter) > vRadius * vRadius)
                continue;
            best = t;
            bestNormal = axis * (k == 0 ? -1.0 : 1.0);
            hitFound = true;
        }
    }

    if (!hitFound)
        discard;

    vec3 hit = rd * best;
    vec3 n = bestNormal;
    if (dot(n, -hit) < 0.0)
        n = -n;                       // always face the viewer, as mesh.frag does

    vec4 clip = uProj * vec4(hit, 1.0);
    gl_FragDepth = (clip.z / clip.w) * 0.5 + 0.5;

    // Axial colour gradient: the tessellated path interpolates iColor ->
    // iColor2 over z in [0, 1] of the unit cylinder, which is exactly the
    // fractional position along the axis here.
    float s = clamp(dot(hit - vBaseView, axis) / height, 0.0, 1.0);
    vec4 baseColor = mix(vColorA, vColorB, s);

    vec3 v = normalize(-hit);

    float specularWeight = 1.0;
    float diffuseWeight  = 1.0;
    float shininess      = uShininess;
    if (finish == FINISH_SHINY) {
        specularWeight = 2.2;
        diffuseWeight  = 0.9;
        shininess      = uShininess * 4.0;
    } else if (finish == FINISH_MATTE) {
        specularWeight = 0.0;
        diffuseWeight  = 1.15;
    } else if (glassy) {
        specularWeight = 1.8;
        diffuseWeight  = 0.75;
        shininess      = uShininess * 2.5;
    }

    vec4 posWorld = uInvView * vec4(hit, 1.0);
    vec4 posLight = vLightSpace * posWorld;
    float visibility = shadowVisibility(n, posLight);
    float primaryFactor = mix(1.0, visibility, clamp(uShadowStrength, 0.0, 1.0));

    vec3 color = vec3(0.0);
    if (uShadingModel == SHADING_PBR) {
        color = shadePbr(n, v, baseColor.rgb, primaryFactor);
    } else if (uShadingModel == SHADING_TOON) {
        color = shadeToon(n, v, baseColor.rgb, primaryFactor);
    } else {
        // Blinn-Phong — the exact loop mesh.frag runs, so this profile is
        // indistinguishable from Legacy except for the geometry it is
        // rasterized from.
        for (int i = 0; i < uLightCount; ++i) {
            vec3 l = normalize(-uLightDir[i]);
            vec3 h = normalize(l + v);
            float ndl = max(dot(n, l), 0.0);
            float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), shininess) : 0.0;
            float direct = (i == 0) ? primaryFactor : 1.0;
            color += baseColor.rgb * uLightAmbient[i]
                   + baseColor.rgb * uLightDiffuse[i] * ndl * diffuseWeight * direct
                   + uLightSpecular[i] * spec * specularWeight * direct;
        }
    }

    float alpha = baseColor.a;
    if (glassy) {
        float fresnel = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 3.0);
        float glass = clamp(uSurfaceOpacity + (1.0 - uSurfaceOpacity) * fresnel,
                            0.0, 1.0);
        alpha = glass * baseColor.a;
        color += uLightSpecular[0] * fresnel * 0.25;
    }
    if (uFogMode != 0) {
        float dist = length(hit);
        float fogVisibility = uFogMode == 1
            ? clamp((uFogEnd - dist) / max(uFogEnd - uFogStart, 1e-3), 0.0, 1.0)
            : exp(-uFogDensity * max(dist - uFogStart, 0.0));
        color = mix(uFogColor, color, fogVisibility);
    }
    fragColor = vec4(color, alpha);
    gNormal = vec4(n * 0.5 + 0.5, 1.0);
}
