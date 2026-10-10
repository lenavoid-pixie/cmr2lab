#version 450
// bg.frag -- the studio backdrop behind the car.
//
// THIS IS MINE, NOT THE GAME'S. CMR2 has no garage: its cars are drawn inside a
// stage, against sky, trees and road. This is a neutral vertical gradient
// chosen so a bare car reads as a photographed object instead of as a model
// floating in the clear colour. It is drawn first, with depth off, uses no
// uniform buffer at all, and it carries no game data of any kind.
layout(location=0) in vec2 vUV;
layout(location=0) out vec4 outColor;
void main(){
    float v = clamp(vUV.y, 0.0, 1.0);
    float u = clamp(vUV.x, 0.0, 1.0);
    vec3 top    = vec3(0.115, 0.128, 0.150);
    vec3 bottom = vec3(0.030, 0.032, 0.038);
    vec3 c = mix(bottom, top, pow(v, 1.35));
    // a faint pool of light behind the car, centred a little above the middle
    float pool = exp(-pow((u - 0.5) * 2.2, 2.0)) * exp(-pow((v - 0.60) * 2.2, 2.0));
    c += pool * vec3(0.060, 0.063, 0.075);
    outColor = vec4(c, 1.0);
}
