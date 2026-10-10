#version 450
// shadow.frag -- the ground shadow. Flat, untextured, no sampler.
//
// MINE, not the game's: CMR2 casts car shadows with its own shadow meshes. This
// is a planar projection of the car's real geometry drawn with the car's own
// vertex shader and a flattened MVP, blended as dst * (1 - alpha).
layout(location=0) out vec4 outColor;
void main(){
    outColor = vec4(0.0, 0.0, 0.0, 0.32);
}
