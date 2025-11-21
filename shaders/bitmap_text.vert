#version 450

// NOTE: Push constants for the char transform and
// the codepoint.
layout(push_constant) uniform TransformPushConstant {
    mat4 transform;
    uint char_ascii_code;
};

// NOTE: Outputs to the fragment shader
layout(location = 0) out vec2 v_uv;

void main() {

    vec2 positions[6] = {
        vec2(-1.0, -1.0),  // Bottom-left
        vec2( 1.0,  1.0),  // Top-right
        vec2(-1.0,  1.0),  // Top-left

        vec2(-1.0, -1.0),  // Bottom-left
        vec2( 1.0, -1.0),  // Bottom-right
        vec2( 1.0,  1.0),  // Top-right
    };

    // NOTE: Monogram-bitmap.png contains 16x8 chars
    const uint bitmap_chars_h = 16;
    const uint bitmap_chars_v = 8;

    const uint char_j = (char_ascii_code - 32) % bitmap_chars_h;
    const uint char_i = (char_ascii_code - 32) / bitmap_chars_h;

    const float uv_height = 1.0 / bitmap_chars_v;
    const float uv_width = 1.0 / bitmap_chars_h;
    const vec2 uv_origin = vec2(char_j * uv_width, char_i * uv_height);

    vec2 uvs[6] = {
        uv_origin + vec2(0, uv_height),
        uv_origin + vec2(uv_width, 0),
        uv_origin,

        uv_origin + vec2(0, uv_height),
        uv_origin + vec2(uv_width, uv_height),
        uv_origin + vec2(uv_width, 0),
    };

    gl_Position = transform * vec4(positions[gl_VertexIndex], 0, 1);
    v_uv = uvs[gl_VertexIndex];
}
