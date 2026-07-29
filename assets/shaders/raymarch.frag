#version 330 core

// Direct volume rendering — fragment stage.
//
// Marches a ray through the scalar field and accumulates emission/absorption
// front-to-back. This shows the WHOLE field rather than one isovalue: a
// density's core, its bonding region and its tail are all present at once,
// weighted by a transfer function, where an isosurface has to pick one level
// and discard everything else.
//
// Front-to-back compositing (rather than back-to-front) is what allows early
// termination: once the accumulated alpha is opaque, nothing behind it can
// contribute and the loop can stop — which is most of the performance on a
// dense field.

in vec3 vTexCoord;
in vec3 vPosView;

uniform sampler3D uVolume;    // the scalar field, normalized to [0,1]
uniform sampler1D uTransfer;  // colour + opacity against normalized value

uniform mat4 uView;       // world -> view, for the G-buffer normal
uniform mat4 uInvModel;   // world -> unit cube, to re-enter texture space
uniform mat4 uInvView;    // view -> world
uniform vec3 uEyeWorld;

uniform int   uSteps;       // samples along the longest diagonal
uniform float uDensity;     // global opacity scale
uniform float uIsoLevel;    // lower cutoff; below this contributes nothing
uniform int   uLightingOn;  // gradient-based shading of the accumulated field

layout(location = 0) out vec4 fragColor;
layout(location = 1) out vec4 gNormal;

/// Central-difference gradient of the field, which is its surface normal
/// wherever the field has structure. Six extra taps per lit sample, so it is
/// optional.
vec3 fieldGradient(vec3 p, float step)
{
    float dx = texture(uVolume, p + vec3(step, 0.0, 0.0)).r
             - texture(uVolume, p - vec3(step, 0.0, 0.0)).r;
    float dy = texture(uVolume, p + vec3(0.0, step, 0.0)).r
             - texture(uVolume, p - vec3(0.0, step, 0.0)).r;
    float dz = texture(uVolume, p + vec3(0.0, 0.0, step)).r
             - texture(uVolume, p - vec3(0.0, 0.0, step)).r;
    return vec3(dx, dy, dz);
}

void main()
{
    // The ray, in the cube's own texture space: entry is this fragment (a
    // point on the box surface), direction is from the eye through it.
    vec3 eyeTex = (uInvModel * vec4(uEyeWorld, 1.0)).xyz;
    vec3 dir = normalize(vTexCoord - eyeTex);

    // Clip the ray to the unit cube (slab method). Doing this analytically
    // rather than stepping until the samples leave [0,1] keeps the step count
    // independent of where the box is entered.
    vec3 invDir = 1.0 / max(abs(dir), vec3(1e-6)) * sign(dir + vec3(1e-20));
    vec3 t0 = (vec3(0.0) - eyeTex) * invDir;
    vec3 t1 = (vec3(1.0) - eyeTex) * invDir;
    vec3 tmin = min(t0, t1);
    vec3 tmax = max(t0, t1);
    float enter = max(max(tmin.x, tmin.y), tmin.z);
    float exitT = min(min(tmax.x, tmax.y), tmax.z);
    // Start at the box surface when the eye is outside it, and at the eye
    // itself when it is inside — otherwise a camera pushed into the volume
    // would march from behind itself and the field would vanish.
    enter = max(enter, 0.0);
    if (exitT <= enter)
        discard;

    int steps = clamp(uSteps, 8, 2048);
    float stride = (exitT - enter) / float(steps);
    float texelStep = 1.0 / float(steps);

    vec4 accumulated = vec4(0.0);
    vec3 firstNormal = vec3(0.0, 0.0, 1.0);
    bool haveNormal = false;

    for (int i = 0; i < steps; ++i) {
        vec3 p = eyeTex + dir * (enter + stride * (float(i) + 0.5));
        float value = texture(uVolume, p).r;
        if (value < uIsoLevel)
            continue;

        vec4 sampled = texture(uTransfer, value);
        // Opacity correction for the step size: a transfer function is defined
        // per unit length, so halving the stride must not double the opacity.
        float alpha = 1.0 - pow(1.0 - clamp(sampled.a * uDensity, 0.0, 1.0),
                                stride * float(steps));
        if (alpha <= 0.0)
            continue;
        vec3 color = sampled.rgb;

        if (uLightingOn == 1) {
            vec3 g = fieldGradient(p, texelStep);
            if (dot(g, g) > 1e-12) {
                // The gradient points UP the field; the surface normal of an
                // isosurface points down it.
                vec3 n = normalize(-g);
                if (!haveNormal) {
                    firstNormal = n;
                    haveNormal = true;
                }
                float ndl = max(dot(n, normalize(vec3(0.4, 0.5, 1.0))), 0.0);
                color *= 0.35 + 0.65 * ndl;
            }
        }

        // Front-to-back: each sample contributes only what the accumulated
        // opacity has not already blocked.
        accumulated.rgb += (1.0 - accumulated.a) * color * alpha;
        accumulated.a += (1.0 - accumulated.a) * alpha;
        if (accumulated.a >= 0.995)
            break; // early termination
    }

    if (accumulated.a <= 0.001)
        discard;
    fragColor = accumulated;
    // The first lit sample's normal stands for the whole ray in the G-buffer.
    // A volume has no single surface, so this is an approximation — but it is
    // the one SSAO can use, and leaving it zero would exclude the volume from
    // occlusion entirely.
    gNormal = haveNormal
        ? vec4(normalize(mat3(uView) * firstNormal) * 0.5 + 0.5, 1.0)
        : vec4(0.0);
}
