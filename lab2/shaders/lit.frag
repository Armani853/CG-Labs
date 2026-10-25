#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    vec4 cameraPosition;
    vec4 ambient;
    vec4 directionalDirection;
    vec4 directionalColor;
    ivec4 counts;
} scene;

struct PointLight {
    vec4 position;
    vec4 colorIntensity;
};
layout(set = 0, binding = 1, std430) readonly buffer PointLights {
    PointLight lights[];
} points;

struct SpotLight {
    vec4 position;
    vec4 directionInner;
    vec4 colorIntensity;
    vec4 outerCos;
};
layout(set = 0, binding = 2, std430) readonly buffer SpotLights {
    SpotLight lights[];
} spots;

layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;

vec3 blinnPhong(vec3 normal, vec3 viewDirection, vec3 lightDirection,
                vec3 color, float intensity) {
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 halfway = normalize(lightDirection + viewDirection);
    float highlight = diffuse > 0.0
        ? pow(max(dot(normal, halfway), 0.0), object.specular.a) : 0.0;
    return intensity * color *
        (object.albedo.rgb * diffuse + object.specular.rgb * highlight);
}

void main() {
    vec3 normal = normalize(worldNormal);
    vec3 viewDirection = normalize(scene.cameraPosition.xyz - worldPosition);
    vec3 result = object.albedo.rgb * scene.ambient.rgb * scene.ambient.a;

    vec3 sunDirection = normalize(-scene.directionalDirection.xyz);
    result += blinnPhong(normal, viewDirection, sunDirection,
                          scene.directionalColor.rgb, scene.directionalColor.a);

    for (int i = 0; i < scene.counts.x; ++i) {
        vec3 toLight = points.lights[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(toLight, toLight), 0.01);
        result += blinnPhong(normal, viewDirection, normalize(toLight),
                              points.lights[i].colorIntensity.rgb,
                              points.lights[i].colorIntensity.a / distanceSquared);
    }

    for (int i = 0; i < scene.counts.y; ++i) {
        vec3 toLight = spots.lights[i].position.xyz - worldPosition;
        float distanceSquared = max(dot(toLight, toLight), 0.01);
        vec3 lightDirection = normalize(toLight);
        float cosine = dot(normalize(-spots.lights[i].directionInner.xyz), lightDirection);
        float innerCos = spots.lights[i].directionInner.w;
        float outerCos = spots.lights[i].outerCos.x;
        float edge = innerCos > outerCos + 0.0001
            ? smoothstep(outerCos, innerCos, cosine)
            : step(innerCos, cosine);
        result += blinnPhong(normal, viewDirection, lightDirection,
                              spots.lights[i].colorIntensity.rgb,
                              spots.lights[i].colorIntensity.a * edge / distanceSquared);
    }

    outColor = vec4(result / (result + vec3(1.0)), 1.0);
}
