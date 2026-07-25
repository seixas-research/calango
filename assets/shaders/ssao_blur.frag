#version 330 core

// SSAO blur — removes the high-frequency noise the rotated sampling kernel
// deliberately traded banding for.
//
// Bilateral rather than a plain box: the blur is weighted by how close each
// tap's depth is to the centre pixel's, so occlusion never bleeds across a
// silhouette. A plain blur here produces dark halos around every atom against
// the background, which reads as a rendering bug rather than as shading.

in vec2 vUv;

uniform sampler2D uAo;
uniform sampler2D uDepth;
uniform vec2  uPixelSize;   // 1 / framebuffer size
uniform int   uRadius;      // taps per side; 2 covers the 4x4 noise tile
uniform float uDepthSigma;  // depth difference (NDC) that halves a tap's weight

out float fragColor;

void main()
{
    float centerDepth = texture(uDepth, vUv).r;
    float total = 0.0;
    float weightSum = 0.0;

    for (int x = -uRadius; x <= uRadius; ++x) {
        for (int y = -uRadius; y <= uRadius; ++y) {
            vec2 uv = vUv + vec2(float(x), float(y)) * uPixelSize;
            float tapDepth = texture(uDepth, uv).r;
            // Gaussian in depth: taps on the far side of an edge contribute
            // essentially nothing.
            float dz = (tapDepth - centerDepth) / max(uDepthSigma, 1e-6);
            float weight = exp(-0.5 * dz * dz);
            total += texture(uAo, uv).r * weight;
            weightSum += weight;
        }
    }

    fragColor = weightSum > 0.0 ? total / weightSum : texture(uAo, vUv).r;
}
