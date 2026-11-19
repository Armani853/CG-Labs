#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0, std140) uniform ModelData {
    mat4 model;
} object;

layout(push_constant) uniform ShadowTransform {
    mat4 lightViewProjection;
} shadow;

void main() {
    gl_Position = shadow.lightViewProjection * object.model * vec4(inPosition, 1.0);
}
