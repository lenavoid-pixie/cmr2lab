#version 450
// bg.vert -- the backdrop. A full-screen triangle in clip space; no game data.
layout(location=0) out vec2 vUV;
void main(){
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vUV = p;
    gl_Position = vec4(p * 2.0 - 1.0, 1.0, 1.0);
}
