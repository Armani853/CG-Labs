#version 450

layout(location = 0) in vec3 inPosition;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
} scene;

layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
} object;

void main() {
    gl_Position = scene.viewProjection * object.model * vec4(inPosition, 1.0);
}
