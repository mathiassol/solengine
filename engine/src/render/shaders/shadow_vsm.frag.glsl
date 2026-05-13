#version 450
layout(location = 0) out vec2 out_moments;
const float EVSM_C = 40.0;
void main() {
    // EVSM negative exponent: background (depth=1.0) maps to ~0, avoiding
    // the bilinear-blending white-halo artefact of raw-depth VSM.
    float w = exp(-EVSM_C * gl_FragCoord.z);
    out_moments = vec2(w, w * w);
}
