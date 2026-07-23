#version 330 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in mat4 iModel;   // per-instance, occupies locations 2..5
layout(location = 6) in vec4 iColor;   // per-instance, color at z = 0
layout(location = 7) in vec4 iColor2;  // per-instance, color at z = 1

uniform mat4 uView;
uniform mat4 uProj;
/// World -> light clip space, for the shadow lookup in mesh.frag. The
/// fragment needs the LIGHT-space position, which cannot be recovered from
/// the view-space one without the inverse view, so it is computed here.
uniform mat4 uLightSpace;

out vec3 vNormalView;
out vec3 vPosView;
out vec4 vColor;
out vec4 vPosLight;

void main()
{
    mat4 modelView = uView * iModel;
    vec4 posView = modelView * vec4(aPos, 1.0);
    vPosView = posView.xyz;
    // Inverse-transpose handles the non-uniform scaling of bond cylinders.
    vNormalView = transpose(inverse(mat3(modelView))) * aNormal;
    // Axial gradient (Gouraud): the unit cylinder spans z in [0, 1], so
    // bond instances blend iColor -> iColor2 end to end; spheres and other
    // meshes pass identical colors and stay uniform.
    vColor = mix(iColor, iColor2, clamp(aPos.z, 0.0, 1.0));
    vPosLight = uLightSpace * iModel * vec4(aPos, 1.0);
    gl_Position = uProj * posView;
}
