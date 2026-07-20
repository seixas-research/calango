#version 330 core

in vec3 vNormalView;
in vec3 vPosView;
in vec4 vColor;

uniform vec3 uLightDirView;

out vec4 fragColor;

void main()
{
    vec3 n = normalize(vNormalView);
    if (!gl_FrontFacing)
        n = -n;

    vec3 l = normalize(-uLightDirView);
    vec3 v = normalize(-vPosView);
    vec3 h = normalize(l + v);

    float diffuse = max(dot(n, l), 0.0);
    float specular = pow(max(dot(n, h), 0.0), 48.0);

    vec3 color = vColor.rgb * (0.28 + 0.72 * diffuse) + vec3(0.30) * specular;
    fragColor = vec4(color, vColor.a);
}
