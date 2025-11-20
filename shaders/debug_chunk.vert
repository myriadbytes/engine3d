#version 450

// NOTE: Vertex attributes
layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_normal;

// NOTE: Outputs to the fragment shader
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec3 v_normal;

// NOTE: Model, view and projection matrices
layout(push_constant) uniform ModelMatPushConstant {
    mat4 model;
};
layout(set = 0, binding = 0) uniform ViewMatBuffer {
    mat4 view;
};
layout(set = 0, binding = 1) uniform ProjMatBuffer {
    mat4 proj;
};

void main() {
    vec4 world_pos = model * vec4(in_position, 1.0);
    gl_Position = proj * view * world_pos;

    vec4 mountain_color = vec4(1.0, 1.0, 1.0, 1.0);
    vec4 grass_color    = vec4(0.3, 0.4, 0.3, 1.0);

    float t = pow(world_pos.y / (16.0 * 3.0), 4.0);

    v_color = mix(grass_color, mountain_color, t);
    v_normal = in_normal;
}
