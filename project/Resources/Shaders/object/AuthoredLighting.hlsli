#ifndef KUBO_AUTHORED_LIGHTING
#define KUBO_AUTHORED_LIGHTING
struct AuthoredLightGpu
{
    float4 colorIntensity;
    float4 positionRange;
    float4 directionOuter;
    float4 parameters;
};
struct AuthoredLightData
{
    uint mode;
    uint count;
    uint2 reserved;
    AuthoredLightGpu lights[64];
};
ConstantBuffer<AuthoredLightData> gAuthoredLights : register(b5);
float3 AuthoredNormalize(float3 value)
{
    float squared = dot(value, value);
    return squared > 1e-16f && isfinite(squared) ? value * rsqrt(squared) : float3(0, 0, 0);
}
// Authored mode uses explicit lights only. Legacy ambient/shadows stay on the
// legacy branch; Point/Spot do not pretend to have authored shadow maps.
float3 EvaluateAuthoredLights(float3 surface, float3 normal, float3 eye, float3 position, float shininess)
{
    normal = AuthoredNormalize(normal);
    float3 view = AuthoredNormalize(eye - position);
    float3 result = float3(0, 0, 0);
    [loop]
    for (uint i = 0; i < min(gAuthoredLights.count, 64u); ++i)
    {
        AuthoredLightGpu light = gAuthoredLights.lights[i];
        float3 direction = -light.directionOuter.xyz;
        float attenuation = 1.0f;
        if (light.parameters.z > 0.5f)
        {
            float3 delta = light.positionRange.xyz - position;
            float radius = light.positionRange.w;
            // Cull before squared distance, including huge finite world poses.
            if (any(abs(delta) > radius)) continue;
            float distance = length(delta);
            if (distance >= radius) continue;
            direction = distance > 1e-6f ? delta / distance : -light.directionOuter.xyz;
            attenuation = pow(saturate(1.0f - distance / radius), light.parameters.y);
            if (light.parameters.z > 1.5f)
            {
                float cosine = dot(-direction, light.directionOuter.xyz);
                if (cosine < light.directionOuter.w) continue;
                float width = light.parameters.x - light.directionOuter.w;
                float cone = width > 1e-6f ? saturate((cosine - light.directionOuter.w) / width) : 1.0f;
                attenuation *= cone * cone * (3.0f - 2.0f * cone);
            }
        }
        float diffuse = saturate(dot(normal, direction));
        float specular = diffuse > 0 ? pow(saturate(dot(normal, AuthoredNormalize(direction + view))), shininess) : 0;
        float3 radiance = light.colorIntensity.rgb * light.colorIntensity.w * attenuation;
        result += radiance * (surface * diffuse + 0.16f * specular);
    }
    return result;
}
#endif
