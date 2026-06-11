#include "Object3d.hlsli"

struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
    float shininess;
};

struct SceneLightData
{
    float4 color;
    float3 direction;
    float intensity;
    float4 ambientColor;
    float ambientIntensity;
    float specularStrength;
    int enable;
    float padding;
};

struct Camera
{
    float3 worldPosition;
};

struct EnvironmentReflectionSetting
{
    float reflectionStrength;
    float roughness;
    float textureInfluence;
    float padding;
};

struct ShadowMapData
{
    float4x4 lightViewProjection;
    float4 settings;
};

ConstantBuffer<Material> gMaterial : register(b0);
ConstantBuffer<SceneLightData> gSceneLight : register(b1);
ConstantBuffer<Camera> gCamera : register(b2);
ConstantBuffer<EnvironmentReflectionSetting> gEnvironment : register(b3);
ConstantBuffer<ShadowMapData> gShadowData : register(b4);

Texture2D<float4> gTexture : register(t0);
TextureCube<float4> gEnvironmentTexture : register(t2);
Texture2D<float> gShadowMap : register(t3);
SamplerState gSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

float CalculateShadow(float3 worldPosition, float3 normal)
{
    if (gShadowData.settings.x < 0.5f)
    {
        return 1.0f;
    }

    float4 lightClip = mul(float4(worldPosition, 1.0f), gShadowData.lightViewProjection);
    float3 projected = lightClip.xyz / lightClip.w;
    float2 uv = float2(projected.x * 0.5f + 0.5f, -projected.y * 0.5f + 0.5f);
    if (projected.z <= 0.0f || projected.z >= 1.0f ||
        any(uv < 0.0f) || any(uv > 1.0f))
    {
        return 1.0f;
    }

    float normalBias = gShadowData.settings.w *
        (1.0f + 1.5f * (1.0f - saturate(dot(normalize(normal), normalize(-gSceneLight.direction)))));
    float visibility = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            visibility += gShadowMap.SampleCmpLevelZero(
                gShadowSampler,
                uv + float2(x, y) * gShadowData.settings.y,
                projected.z - normalBias);
        }
    }
    return lerp(1.0f, visibility / 9.0f, saturate(gShadowData.settings.z));
}

PixelShaderOutput main(VertexShaderOutput input)
{
    float4 transformedUV = mul(float4(input.texcoord, 0.0f, 1.0f), gMaterial.uvTransform);
    float4 textureColor = gTexture.Sample(gSampler, transformedUV.xy);
    if (textureColor.a <= 0.5f)
    {
        discard;
    }

    PixelShaderOutput output;
    float3 surfaceColor = gMaterial.color.rgb *
        lerp(float3(1.0f, 1.0f, 1.0f), textureColor.rgb, saturate(gEnvironment.textureInfluence));
    if (gMaterial.enableLighting == 0 || gSceneLight.enable == 0)
    {
        output.color = float4(surfaceColor, gMaterial.color.a * textureColor.a);
        return output;
    }

    float3 normal = normalize(input.normal);
    float3 lightDirection = normalize(-gSceneLight.direction);
    float3 toEye = normalize(gCamera.worldPosition - input.worldPosition);
    float diffuseFactor = saturate(dot(normal, lightDirection));
    float3 halfVector = normalize(lightDirection + toEye);
    float specularFactor = pow(saturate(dot(normal, halfVector)), gMaterial.shininess);
    float shadow = CalculateShadow(input.worldPosition, normal);
    float3 ambient =
        surfaceColor * gSceneLight.ambientColor.rgb * gSceneLight.ambientIntensity;
    float3 diffuse =
        surfaceColor * gSceneLight.color.rgb * gSceneLight.intensity * diffuseFactor * shadow;
    float3 specular =
        gSceneLight.color.rgb * gSceneLight.intensity *
        gSceneLight.specularStrength * specularFactor * shadow;
    float3 cameraToPosition = normalize(input.worldPosition - gCamera.worldPosition);
    float3 reflectedVector = reflect(cameraToPosition, normal);
    float lod = saturate(gEnvironment.roughness) * 6.0f;
    float3 environmentColor =
        gEnvironmentTexture.SampleLevel(gSampler, reflectedVector, lod).rgb *
        gEnvironment.reflectionStrength;

    output.color = float4(
        ambient + diffuse + specular + environmentColor,
        gMaterial.color.a * textureColor.a);
    return output;
}
