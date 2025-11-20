#version 450

// NOTE: Inputs from the vertex shader
layout(location = 0) in vec4 v_color;
layout(location = 1) in vec3 v_normal;

// NOTE: Output to the framebuffer
layout(location = 0) out vec4 out_color;

void main() {
    vec3 fake_light_dir = normalize(vec3(-0.7, 1.2, -1.9));
    float diffuse = max(dot(v_normal, fake_light_dir), 0.0);
    float ambient = 0.2;

    out_color = v_color * (diffuse + ambient);
}
