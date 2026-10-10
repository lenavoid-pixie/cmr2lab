#version 450
layout(location=0) in vec3 vNormal;
layout(location=1) in vec4 vColor;
layout(location=2) in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(set=2, binding=0) uniform sampler2D tex;
void main(){
    vec3 n = normalize(vNormal);
    vec3 L = normalize(vec3(-0.45, 0.85, 0.62));
    float lam = max(dot(n, L), 0.0);
    vec4 t = texture(tex, vUV);
    outColor = vec4(t.rgb * (0.35 + 0.65*lam), 1.0);
}
