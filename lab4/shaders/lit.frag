#version 450

layout(location = 0) in vec3 worldPosition;
layout(location = 1) in vec3 worldNormal;
layout(location = 2) in vec2 textureUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0, std140) uniform SceneData {
    mat4 viewProjection;
    mat4 sunViewProjection;
    mat4 spotViewProjection;
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
layout(set = 0, binding = 3) uniform sampler2DShadow sunShadowMap;
layout(set = 0, binding = 4) uniform sampler2DShadow spotShadowMap;

layout(set = 1, binding = 0, std140) uniform ModelData {
    mat4 model;
    mat4 normalMatrix;
    vec4 albedo;
    vec4 specular;
} object;

layout(set = 1, binding = 1) uniform sampler2D albedoTexture;
layout(set = 1, binding = 2) uniform sampler2D specularTexture;
layout(set = 1, binding = 3) uniform sampler2D emissiveTexture;
layout(push_constant) uniform EffectData {
    float time;
    float strength;
    float emissiveScale;
    int enabled;
    int floor;
} effect;

vec3 surfaceAlbedo;
vec3 surfaceSpecular;

float shadowVisibility(sampler2DShadow depthMap, mat4 lightMatrix) {
    vec4 clip = lightMatrix * vec4(worldPosition, 1.0);
    if (clip.w <= 0.0)
        return 1.0;
    vec3 ndc = clip.xyz / clip.w;
    // The projection already flips Y. Vulkan depth is already in [0, 1].
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (ndc.z <= 0.0 || ndc.z >= 1.0 ||
        any(lessThan(uv, vec2(0.0))) || any(greaterThanEqual(uv, vec2(1.0))))
        return 1.0;
    vec2 texel = 1.0 / vec2(textureSize(depthMap, 0));
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            visible += textureLod(depthMap, vec3(uv + vec2(x, y) * texel, ndc.z), 0.0);
    return visible / 9.0;
}

vec3 blinnPhong(vec3 normal, vec3 viewDirection, vec3 lightDirection,
                vec3 color, float intensity) {
    float diffuse = max(dot(normal, lightDirection), 0.0);
    vec3 halfway = normalize(lightDirection + viewDirection);
    float highlight = diffuse > 0.0
        ? pow(max(dot(normal, halfway), 0.0), object.specular.a) : 0.0;
    return intensity * color *
        (surfaceAlbedo * diffuse + surfaceSpecular * highlight);
}

void main() {
    vec2 uv = textureUV;
    vec3 albedoSample = texture(albedoTexture, uv).rgb;
    if (effect.enabled != 0) {
        // Three independent UV samples produce an animated coloured weave.
        vec2 wave = effect.strength * vec2(
            sin(uv.y * 17.0 + effect.time * 2.1),
            cos(uv.x * 13.0 - effect.time * 1.7));
        vec3 a = texture(albedoTexture, uv + wave).rgb;
        vec3 b = texture(albedoTexture, uv + vec2(wave.y, -wave.x) + vec2(.16, 0)).rgb;
        vec3 c = texture(albedoTexture, uv - wave * 1.5 + vec2(0, .19)).rgb;
        vec3 tint = .5 + .5 * cos(effect.time + vec3(0, 2.094, 4.188));
        albedoSample = mix(a, .4 * a + .35 * b + .25 * c * (0.65 + tint), .85);
    }
    surfaceAlbedo = albedoSample * object.albedo.rgb;
    surfaceSpecular = texture(specularTexture, uv).rgb * object.specular.rgb;
    if (effect.floor != 0) {
        vec2 seam = min(fract(uv), 1.0 - fract(uv));
        vec2 tiles = smoothstep(vec2(0.015), vec2(0.015) + fwidth(uv), seam);
        float checker = mod(floor(uv.x) + floor(uv.y), 2.0);
        surfaceAlbedo = object.albedo.rgb * mix(0.63, mix(0.90, 1.0, checker), tiles.x * tiles.y);
        surfaceSpecular = vec3(0.0);
    }
    vec3 normal = normalize(worldNormal);
    vec3 viewDirection = normalize(scene.cameraPosition.xyz - worldPosition);
    vec3 result = surfaceAlbedo * scene.ambient.rgb * scene.ambient.a;

    vec3 sunDirection = normalize(-scene.directionalDirection.xyz);
    float sunVisibility = scene.counts.z != 0 ? shadowVisibility(sunShadowMap, scene.sunViewProjection) : 1.0;
    result += sunVisibility * blinnPhong(normal, viewDirection, sunDirection,
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
        float spotVisibility = (i == 0 && scene.counts.w != 0 && edge > 0.0)
            ? shadowVisibility(spotShadowMap, scene.spotViewProjection) : 1.0;
        result += spotVisibility * blinnPhong(normal, viewDirection, lightDirection,
                              spots.lights[i].colorIntensity.rgb,
                              spots.lights[i].colorIntensity.a * edge / distanceSquared);
    }

    if (effect.floor == 0)
        result += texture(emissiveTexture, uv).rgb * effect.emissiveScale;
    outColor = vec4(result / (result + vec3(1.0)), 1.0);
}
