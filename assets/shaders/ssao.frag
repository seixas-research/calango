#version 330 core

// Screen-space ambient occlusion (View -> Visual Effects).
//
// The scene is rendered into a G-buffer (color + view-space normals + depth);
// this pass estimates, for every visible fragment, how much of the hemisphere
// above it is blocked by nearby geometry. That is what darkens the crevices
// between touching spheres and the gaps under a bond — contact shading a
// direct-lighting model cannot produce, because those points are still fully
// lit by every light in the scene.
//
// Method: sample a hemisphere oriented along the surface normal, project each
// sample back to screen space, and compare its depth against what the depth
// buffer actually holds there. A sample that lands behind existing geometry is
// occluded. The kernel is rotated per-pixel by a small tiled noise texture,
// which trades banding for high-frequency noise that the blur pass removes.

in vec2 vUv;

uniform sampler2D uDepth;
uniform sampler2D uNormal;   // rgb = view normal * 0.5 + 0.5, a = validity
uniform sampler2D uNoise;    // 4x4 tiled random rotation vectors

uniform mat4 uProjection;
uniform mat4 uInvProjection;

#define MAX_KERNEL 64
uniform vec3  uKernel[MAX_KERNEL];
uniform int   uKernelSize;
uniform float uRadius;      // world-space sampling radius (Å)
uniform float uBias;        // depth bias, kills self-occlusion acne
uniform vec2  uNoiseScale;  // framebuffer size / noise texture size

out float fragColor;

// Reconstruct the view-space position of a fragment from its depth. Cheaper
// and more accurate than carrying a position G-buffer: depth is already there
// at full precision, and the inverse projection is a single mat4 multiply.
vec3 viewPosition(vec2 uv, float depth)
{
    vec4 ndc = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    vec4 view = uInvProjection * ndc;
    return view.xyz / view.w;
}

void main()
{
    vec4 encodedNormal = texture(uNormal, vUv);
    float depth = texture(uDepth, vUv).r;
    // a < 0.5 marks geometry with no meaningful normal (lines, wireframe) and
    // the cleared background; depth >= 1 is the far plane. Both are reported
    // fully unoccluded so the composite leaves them untouched.
    if (encodedNormal.a < 0.5 || depth >= 1.0) {
        fragColor = 1.0;
        return;
    }

    vec3 position = viewPosition(vUv, depth);
    vec3 normal = normalize(encodedNormal.xyz * 2.0 - 1.0);

    // Gram-Schmidt a tangent basis around the normal, rotated by the noise so
    // neighbouring pixels use different sample directions.
    vec3 randomVec = normalize(texture(uNoise, vUv * uNoiseScale).xyz);
    vec3 tangent = normalize(randomVec - normal * dot(randomVec, normal));
    vec3 bitangent = cross(normal, tangent);
    mat3 tbn = mat3(tangent, bitangent, normal);

    float occlusion = 0.0;
    for (int i = 0; i < uKernelSize; ++i) {
        vec3 samplePos = position + tbn * uKernel[i] * uRadius;

        vec4 offset = uProjection * vec4(samplePos, 1.0);
        offset.xyz /= offset.w;
        vec2 sampleUv = offset.xy * 0.5 + 0.5;
        // Samples that leave the screen have no depth to test against;
        // counting them as occluded would darken the frame edges.
        if (sampleUv.x < 0.0 || sampleUv.x > 1.0
            || sampleUv.y < 0.0 || sampleUv.y > 1.0)
            continue;

        float sampleDepth = texture(uDepth, sampleUv).r;
        if (sampleDepth >= 1.0)
            continue;
        vec3 occluder = viewPosition(sampleUv, sampleDepth);

        // View space looks down -z, so a LARGER z is nearer the camera: the
        // sample is occluded when real geometry sits in front of it.
        // rangeCheck stops a distant foreground object from shadowing a
        // background one that merely happens to be behind it on screen.
        float rangeCheck =
            smoothstep(0.0, 1.0, uRadius / max(abs(position.z - occluder.z), 1e-4));
        occlusion += (occluder.z >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
    }

    fragColor = 1.0 - occlusion / float(max(uKernelSize, 1));
}
