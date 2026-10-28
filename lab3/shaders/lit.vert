#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColor;
    ivec4 counts;
} scene;

layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;

layout(location = 0) out vec3 worldPosition;
layout(location = 1) out vec3 worldNormal;
layout(location = 2) out vec2 textureUV;

void main() {
    vec4 world = object.model * vec4(inPosition, 1.0);
    worldPosition = world.xyz;
    worldNormal = normalize(mat3(object.normalMatrix) * inNormal);
    textureUV = inUV;
    gl_Position = scene.viewProjection * world;
}
