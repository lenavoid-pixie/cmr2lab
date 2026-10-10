#version 450
// car.frag -- fixed-function-shaped shading, the shape Direct3D 7 gives CMR2.
//
// THE GAME'S PART, and where it comes from:
//   * alpha test on, ALPHAFUNC = GREATER, ALPHAREF 0x80 while blending is off
//     and 1 while it is on -- Graphics_SwitchAlphaBlendAndTest (0x0049dc80) and
//     the device init at Graphics.cpp:1888-1902.
//   * blend pair (5,6) = SRCALPHA / INVSRCALPHA -- Graphics.cpp:6774,
//     Sprite.cpp:561.
//   * the texture stage is MODULATE (D3DTSS_COLOROP = 3 on all three stages,
//     Graphics.cpp:1914-1919) with a white material -- Scene_RestoreLights
//     (0x004b2e50) sets g_sceneMaterial diffuse and ambient to 1,1,1 -- and
//     D3DRS_COLORVERTEX = FALSE for the mesh path, so the per-vertex colour word
//     is NOT part of the diffuse term (Graphics_SetLightingMode mode 2, which
//     is what Graphics_DrawMeshLOD selects for a car mesh).
//   * the colour target is a plain 32-bit surface with no gamma correction
//     anywhere, so this shader does no gamma correction either.
//
// MINE, not the game's: the ambient value, the key light direction and gain and
// the fill light. CMR2's object light is a stage light and there is no stage
// here. All four are printed by the viewer before the first frame.
layout(location=0) in  vec3 vNormal;
layout(location=1) in  vec4 vColor;
layout(location=2) in  vec2 vUV;
layout(location=0) out vec4 outColor;
layout(set=1, binding=0) uniform UBO {
    mat4 mvp;
    vec4 light;     // xyz = normalised direction toward the key light, w = key gain
    vec4 light2;    // xyz = normalised direction toward the fill light, w = fill gain
    vec4 eye;       // xyz = camera position in world space
    vec4 params;    // x ambient, y = 1 (reserved), z alpha ref, w mode 0/1/2
} ubo;
layout(set=2, binding=0) uniform sampler2D tex;

void main(){
    // mode 2 = the ground shadow pass: a flat silhouette, no texture read
    if (ubo.params.w > 1.5) {
        outColor = vec4(0.0, 0.0, 0.0, 0.42);
        return;
    }
    vec4 t = texture(tex, vUV);
    if (t.a <= ubo.params.z) discard;                 // ALPHATEST + D3DCMP_GREATER
    vec3 n = normalize(vNormal);
    // The key light keeps D3D7's straight lambert. The fill is a half-lambert
    // wrap (0.5 + 0.5 * dot) so that the side facing away from the key still
    // reads as a surface instead of as a silhouette. The wrap is MINE; D3D7 has
    // ambient and lights and nothing else.
    float key  = max(dot(n, normalize(ubo.light.xyz)), 0.0)  * ubo.light.w;
    float fill = (0.5 + 0.5 * dot(n, normalize(ubo.light2.xyz))) * ubo.light2.w;
    float shade = ubo.params.x + key + fill;
    vec3 base = t.rgb;
    // mode 1: the file's per-vertex colour word as an extra diffuse term. That
    // is the shape of D3DRS_COLORVERTEX = TRUE, which is NOT the car path.
    if (ubo.params.w > 0.5) base *= vColor.rgb;
    outColor = vec4(base * shade, t.a);
}
