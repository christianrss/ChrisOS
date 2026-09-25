#version 330
layout(location = 0) in vec4 position;
layout(location = 1) in vec4 color;
out vec4 vcolor;
void main() {
    vcolor = color;
    gl_Position = position;
}
