#version 330 core

// Blinn-Phong with up to 4 independent directional lights.
// Light directions are in VIEW space (camera-relative), so lighting stays
// fixed relative to the viewer while the camera orbits the structure.

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

out vec4 fragColor;

void main()
{
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;
    vec3 v = normalize(-vPosView);

    vec3 color = vec3(0.0);
    for (int i = 0; i < uLightCount; ++i) {
        vec3 l = normalize(-uLightDir[i]);
        vec3 h = normalize(l + v);
        float ndl = max(dot(n, l), 0.0);
        float spec = ndl > 0.0 ? pow(max(dot(n, h), 0.0), uShininess) : 0.0;
        color += vColor.rgb * uLightAmbient[i]
               + vColor.rgb * uLightDiffuse[i] * ndl
               + uLightSpecular[i] * spec;
    }
    fragColor = vec4(color, vColor.a);
}
