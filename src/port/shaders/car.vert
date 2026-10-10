#version 450
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;
layout(location=2) in vec4 inColor;
layout(location=3) in vec2 inUV;
layout(location=0) out vec3 vNormal;
layout(location=1) out vec4 vColor;
layout(location=2) out vec2 vUV;
layout(set=1, binding=0) uniform UBO { mat4 mvp; vec4 light; } ubo;
void main(){
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vNormal = inNormal;
    vColor  = inColor;
    vUV     = inUV;
}
