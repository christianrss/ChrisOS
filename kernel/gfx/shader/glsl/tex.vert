#version 330
layout(location = 0) in vec4 position;
layout(location = 1) in vec2 uv;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec2 fragUV;
void main() {
    fragUV = uv;
    gl_Position = projection * view * model * position;
}
