#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
out vec3 vN;
out vec3 vP;
void main() {
    vec4 world = model * vec4(position, 1.0);
    vP = world.xyz;
    vN = normal;
    gl_Position = projection * view * world;
}
