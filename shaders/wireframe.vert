#version 450

// NOTE: Vertex attributes.
layout (location = 0) in vec3 in_position;

// NOTE: Outputs to the fragment shader.
layout(location = 0) out vec4 v_color;

// NOTE: Model and color in push constants,
// view and projection matrices as uniform buffers.
layout(push_constant) uniform ModelMatPushConstant {
    mat4 model;
    vec4 color;
};
layout(set = 0, binding = 0) uniform ViewMatBuffer {
    mat4 view;
};
layout(set = 0, binding = 1) uniform ProjMatBuffer {
    mat4 proj;
};

void main()
{
    gl_Position = proj * view * model * vec4(in_position, 1);
    v_color = color;
}
