#version 330
in vec3 vN;
in vec3 vP;
in vec2 vUV;
uniform sampler2D diffuseTexture;
uniform vec3 lightPosition;
uniform vec3 lightColor;
uniform vec3 ambientColor;
out vec4 color;
void main() {
    vec3 N = normalize(vN);
    vec3 L = normalize(lightPosition - vP);
    float diffuse = max(dot(N, L), 0.0);
    vec3 result = ambientColor + diffuse * lightColor;
    color = texture(diffuseTexture, vUV) * vec4(result, 1.0);
}
