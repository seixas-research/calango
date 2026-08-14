#version 330 core

// Ground plane ("floor", View -> Visual Effects -> Floor): the surface an
// isolated molecule rests on, so the scene reads as an object in a space
// rather than one floating in a void.
//
// The plane's orientation is a setting (Floor tab): uFloorU and uFloorV span
// it and uFloorNormal is perpendicular to both, a right-handed frame the CPU
// builds once per frame. The DEFAULT is the horizontal xy plane — Calango is
// a Z-UP application: the default camera is (yaw 0, pitch -70, roll 20),
// twenty degrees off the XZ alignment whose screen-up is +z, and the
// crystallographic c axis is the one a figure stands on.
//
// Passing the frame in rather than deriving it here is what keeps the quad's
// winding valid at every orientation: (u, v, n) is right-handed, so the one
// winding baked into the vertex buffer always faces along n.
//
// The geometry is ONE static quad spanning -1..+1, uploaded once at
// initialize() and never touched again: its world placement — centre, height
// and extent — arrives entirely through uniforms. Moving the floor, resizing
// it to a new structure or nudging the height offset therefore costs no
// buffer upload at all, which is what lets it follow the structure live while
// the user edits.

layout(location = 0) in vec2 aCorner; // -1 .. +1 in both axes

uniform mat4 uView;
uniform mat4 uProj;
/// World -> light clip space, for the shadow lookup in floor.frag. Same matrix
/// the depth pass was rendered with; the floor is a shadow RECEIVER only and
/// is deliberately absent from that pass (a plane below everything has nothing
/// to occlude, and including it only invites self-shadowing acne).
uniform mat4 uLightSpace;
uniform vec3 uFloorCenter; ///< world position of the plane's centre
uniform vec3 uFloorU;      ///< unit in-plane axis, the quad's local +x
uniform vec3 uFloorV;      ///< unit in-plane axis, the quad's local +y
uniform vec3 uFloorNormal; ///< unit plane normal; u x v = n
uniform float uHalfSize;   ///< world half-extent of the quad, angstrom

out vec3 vPosView;
out vec3 vNormalView;
out vec4 vPosLight;
/// Offset from the floor centre in the plane's own (u, v) axes. Interpolated
/// as a VECTOR and turned into a radius per fragment: interpolating the length
/// itself across a 4-vertex quad would bow the fade into a diamond, because
/// length() is not linear in the corner positions.
out vec2 vOffsetUV;

void main()
{
    // The corner IS the in-plane offset, in the plane's own axes — so the
    // fade below stays a circle whatever the plane is turned to, with no
    // trigonometry and no per-orientation special case.
    vOffsetUV = aCorner * uHalfSize;
    vec3 world = uFloorCenter + uFloorU * vOffsetUV.x + uFloorV * vOffsetUV.y;
    vec4 posView = uView * vec4(world, 1.0);
    vPosView = posView.xyz;
    // The frame is orthonormal and the plane is never scaled, so the view
    // rotation alone carries its normal — no inverse-transpose is needed here
    // (unlike mesh.vert, which has to undo the bond cylinders' axis scaling).
    vNormalView = mat3(uView) * uFloorNormal;
    vPosLight = uLightSpace * vec4(world, 1.0);
    gl_Position = uProj * posView;
}
