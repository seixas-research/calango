#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 iModel;   // per-instance, occupies locations 2..5
layout(location = 6) in vec4 iColor;   // per-instance

uniform mat4 uView;
uniform mat4 uProj;

out vec3 vNormalView;
out vec3 vPosView;
out vec4 vColor;

void main()
{
    mat4 modelView = uView * iModel;
    vec4 posView = modelView * vec4(aPos, 1.0);
    vPosView = posView.xyz;
    // Inverse-transpose handles the non-uniform scaling of bond cylinders.
    vNormalView = transpose(inverse(mat3(modelView))) * aNormal;
    vColor = iColor;
    gl_Position = uProj * posView;
}
