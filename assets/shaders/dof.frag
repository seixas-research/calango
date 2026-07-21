#version 330 core

// Depth-of-field composite (View -> Visual Effects): the scene is
// rendered into an offscreen color+depth pair; this pass blurs each
// pixel with a Poisson disc whose radius grows with the distance from
// the focal plane (circle of confusion).

in vec2 vUv;

uniform sampler2D uColor;
uniform sampler2D uDepth;
uniform float uNear;
uniform float uFar;
uniform float uFocusDistance;  // view-space focal plane (camera target)
uniform float uFocusRange;     // depth band that stays sharp
uniform float uStrength;       // max blur radius in pixels
uniform vec2 uPixelSize;       // 1 / framebuffer size

float linearDepth(float z)
{
    float ndc = z * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - ndc * (uFar - uNear));
}

const vec2 kTaps[12] = vec2[](
    vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457),
    vec2(-0.203,  0.621), vec2( 0.962, -0.195), vec2( 0.473, -0.480),
    vec2( 0.519,  0.767), vec2( 0.185, -0.893), vec2( 0.507,  0.064),
    vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

out vec4 fragColor;

void main()
{
    float depth = linearDepth(texture(uDepth, vUv).r);
    float coc = clamp(abs(depth - uFocusDistance) / max(uFocusRange, 1e-3),
                      0.0, 1.0);
    float radius = coc * uStrength;

    vec3 color = texture(uColor, vUv).rgb;
    if (radius < 0.5) {
        fragColor = vec4(color, 1.0);
        return;
    }
    vec3 sum = color;
    float weight = 1.0;
    for (int i = 0; i < 12; ++i) {
        vec2 offset = kTaps[i] * radius * uPixelSize;
        // Weigh foreground-sharp samples down so in-focus objects do not
        // bleed into blurred neighbors.
        float sampleDepth = linearDepth(texture(uDepth, vUv + offset).r);
        float sampleCoc = clamp(abs(sampleDepth - uFocusDistance)
                                    / max(uFocusRange, 1e-3),
                                0.0, 1.0);
        float w = max(sampleCoc, 0.15);
        sum += texture(uColor, vUv + offset).rgb * w;
        weight += w;
    }
    fragColor = vec4(sum / weight, 1.0);
}
