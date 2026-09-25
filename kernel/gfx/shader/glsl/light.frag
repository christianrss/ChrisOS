#version 330
in vec3 vN;
in vec3 vP;
uniform vec3 lightPosition;
uniform vec3 lightColor;
uniform vec3 ambientColor;
out vec4 color;
void main() {
    vec3 N = normalize(vN);
    vec3 L = normalize(lightPosition - vP);
    float diffuse = max(dot(N, L), 0.0);
    vec3 result = ambientColor + diffuse * lightColor;
    color = vec4(result, 1.0);
}
