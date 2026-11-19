#version 450

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;

void main() {
    outColor = vec4(object.albedo.rgb, 1.0);
}
