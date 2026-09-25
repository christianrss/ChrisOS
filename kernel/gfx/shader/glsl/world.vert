#version 330
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 uv;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;
out vec3 vN;
out vec3 vP;
out vec2 vUV;
void main() {
    vec4 world = model * vec4(position, 1.0);
    vP = world.xyz;
    vN = normalize(normalMatrix * normal);
    vUV = uv;
    gl_Position = projection * view * world;
}
