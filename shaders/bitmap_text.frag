#version 450

// NOTE: Inputs from the vertex shader
layout(location = 0) in vec2 v_uv;

// NOTE: Output to the framebuffer
layout(location = 0) out vec4 out_color;

// NOTE: Combined image-sampler for the bitmap font.
layout(set = 0, binding = 0) uniform sampler2D font_texture;

void main() {
    out_color = texture(font_texture, v_uv);
}
