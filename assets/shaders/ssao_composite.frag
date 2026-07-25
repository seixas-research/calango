#version 330 core

// Apply the blurred ambient-occlusion term to the shaded scene color.
//
// A straight multiply would drive deep creases to black, because the AO factor
// already multiplies light the direct terms contributed. uIntensity blends
// between "no occlusion" and the full factor, so the control is a strength dial
// rather than an on/off, and the darkest reachable value stays a shadow rather
// than a hole.

in vec2 vUv;

uniform sampler2D uColor;
uniform sampler2D uAo;
uniform float uIntensity; // 0 = AO off, 1 = full occlusion factor

out vec4 fragColor;

void main()
{
    vec4 color = texture(uColor, vUv);
    float ao = texture(uAo, vUv).r;
    float factor = mix(1.0, ao, clamp(uIntensity, 0.0, 1.0));
    fragColor = vec4(color.rgb * factor, color.a);
}
