#version 450
// car.vert -- CMR2 car viewer, vertex stage.
// Straight pass-through: the mesh on the GPU is already in the file's own
// metres (positions, normals, uv set 0), and the only transform is the MVP.
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec4 inColor;     // vertex colour word at +24, file order
layout(location=3) in vec2 inUV;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec4 vColor;
layout(location=2) out vec2 vUV;
layout(set=1, binding=0) uniform UBO {
    mat4 mvp;
    vec4 light;     // xyz = key light direction, w = key gain
    vec4 light2;    // xyz = fill light direction, w = fill gain
    vec4 eye;       // xyz = camera position in world space
    vec4 params;    // x ambient, y reserved, z alpha ref, w mode (0/1/2)
} ubo;
void main(){
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vColor  = inColor;
    vUV     = inUV;
}
