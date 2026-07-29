#version 330 core

// Lit volumetric isosurface — the shading the CPU used to bake, done here.
//
// It reads the SAME light uniforms as mesh.frag, which is the point: an
// isosurface and the atoms it encloses are lit by one set of lights, so the
// surface sits in the scene instead of looking like a decal pasted over it.
// The old baked path used its own hard-coded studio directions, and the
// mismatch showed as soon as the scene lighting was edited.
//
// Two-sided on purpose: an isosurface is routinely viewed from inside (an
// orbital lobe, a cavity in a density), and a one-sided normal turns the
// inner wall black.

#define MAX_LIGHTS 4

in vec3 vNormalView;
in vec3 vPosView;
in vec3 vColor;

uniform int   uLightCount;
uniform vec3  uLightDir[MAX_LIGHTS];
uniform vec3  uLightAmbient[MAX_LIGHTS];
uniform vec3  uLightDiffuse[MAX_LIGHTS];
uniform vec3  uLightSpecular[MAX_LIGHTS];

// Shading model, from the Edit Volumetric Render dialog. Kept as an enum
// rather than folded into "roughness" because Flat is not a rough surface —
// it is the deliberate absence of shading, which is what a figure wants when
// the surface is there to show an isovalue rather than a shape.
//   0 = Flat    : unshaded fill, the historical look
//   1 = Diffuse : Lambert only
//   2 = Glossy  : Lambert + Blinn-Phong highlight
#define SHADING_FLAT    0
#define SHADING_DIFFUSE 1
#define SHADING_GLOSSY  2
uniform int   uShadingMode;
uniform float uAmbient;    // floor for faces turned away from every light
uniform float uSpecular;   // Glossy highlight strength
uniform float uShininess;
uniform float uAlpha;

layout(location = 0) out vec4 fragColor;
// G-buffer attachment 1: the view-space normal for SSAO. The old unlit path
// wrote alpha = 0 here ("no normal available"), so isosurfaces were invisible
// to ambient occlusion — they neither occluded the atoms nor received contact
// shading from them. With a real normal they now participate.
layout(location = 1) out vec4 gNormal;

void main()
{
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;

    if (uShadingMode == SHADING_FLAT) {
        fragColor = vec4(vColor, uAlpha);
        // Still a valid normal for SSAO even when the surface itself is drawn
        // unshaded: the two are independent questions.
        gNormal = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }

    vec3 v = normalize(-vPosView);
    vec3 lit = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        float ndl = max(dot(n, l), 0.0);
        lit += vColor * uLightAmbient[i];
        lit += vColor * uLightDiffuse[i] * ndl;
        if (uShadingMode == SHADING_GLOSSY && ndl > 0.0) {
            vec3 h = normalize(l + v);
            float spec = pow(max(dot(n, h), 0.0), max(uShininess, 1.0));
            lit += uLightSpecular[i] * spec * uSpecular;
        }
    }
    // The ambient floor is applied as a MINIMUM against the lit result rather
    // than added to it: adding would wash out a fully lit face, while the
    // control exists to stop an unlit one going black — on a translucent
    // surface a black facet reads as a hole rather than as shadow.
    vec3 color = max(lit, vColor * uAmbient);

    fragColor = vec4(color, uAlpha);
    gNormal = vec4(n * 0.5 + 0.5, 1.0);
}
