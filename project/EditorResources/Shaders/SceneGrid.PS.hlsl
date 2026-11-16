cbuffer GridCamera : register(b0)
{
    row_major float4x4 viewProjection;
    row_major float4x4 inverseViewProjection;
};

struct GridPixel
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

float2 LineCoverage(float2 position, float2 pixelWidth, float halfWidth)
{
    const float2 distance = abs(frac(position + 0.5) - 0.5) / pixelWidth;
    return 1 - smoothstep(halfWidth, halfWidth + 1, distance);
}

float GridOpacity(float2 position, float2 pixelWidth, float fine, float blend, float halo)
{
    const float2 minor = LineCoverage(position / fine, pixelWidth / fine, 0.35 + halo);
    const float2 major = LineCoverage(position / (fine * 10), pixelWidth / (fine * 10), 0.6 + halo);
    const float2 coarse = LineCoverage(position / (fine * 100), pixelWidth / (fine * 100), 0.6 + halo);
    return max(max(minor.x, minor.y) * 0.24 * (1 - blend),
        max(max(major.x, major.y) * lerp(0.52, 0.24, blend), max(coarse.x, coarse.y) * 0.52 * blend));
}

GridPixel main(float4 pixel : SV_Position, float2 clipXY : TEXCOORD0)
{
    const float4 nearH = mul(float4(clipXY, 0, 1), inverseViewProjection);
    const float4 farH = mul(float4(clipXY, 1, 1), inverseViewProjection);
    const float3 nearPoint = nearH.xyz / nearH.w;
    const float3 farPoint = farH.xyz / farH.w;
    const float3 ray = farPoint - nearPoint;
    const float safeY = (ray.y < 0 ? -1 : 1) * max(abs(ray.y), 0.00001);
    const float intersection = -nearPoint.y / safeY;
    const float3 world = nearPoint + ray * intersection;
    const float2 pixelWidth = max(fwidth(world.xz), 0.00001);
    if (abs(ray.y) < 0.00001 || intersection < 0 || intersection > 1)
        discard;

    // Fade the finer tier before its antialiasing and halo cover the gaps.
    const float level = clamp(log10(max(pixelWidth.x, pixelWidth.y) * 24), -4, 5);
    const float fine = pow(10, floor(level));
    const float blend = smoothstep(0, 1, frac(level));
    float opacity = GridOpacity(world.xz, pixelWidth, fine, blend, 0);
    const float outline = GridOpacity(world.xz, pixelWidth, fine, blend, 0.75) * 0.56;

    const float2 axis = 1 - smoothstep(0.65, 1.65, abs(world.xz) / pixelWidth);
    float3 color = float3(0.42, 0.45, 0.50);
    color = lerp(color, float3(0.16, 0.44, 0.94), axis.x); // Z axis
    color = lerp(color, float3(0.90, 0.18, 0.19), axis.y); // X axis
    opacity = max(opacity, max(axis.x, axis.y) * 0.8);
    const float combined = opacity + outline * (1 - opacity);
    color = (color * opacity + float3(0.02, 0.025, 0.04) * outline * (1 - opacity)) / max(combined, 0.0001);
    const float fadeEnd = clamp(abs(nearPoint.y) * 26, 28, 3000);
    const float fade = (1 - smoothstep(fadeEnd * 0.48, fadeEnd, length(world.xz - nearPoint.xz))) *
        (1 - smoothstep(0.7, 1.0, intersection));
    clip(combined * fade - 0.003);
    const float4 projected = mul(float4(world, 1), viewProjection);
    GridPixel output;
    output.color = float4(color, combined * fade);
    output.depth = saturate(projected.z / projected.w);
    return output;
}
