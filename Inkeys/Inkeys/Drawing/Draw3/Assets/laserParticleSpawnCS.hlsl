// laserParticleSpawnCS.hlsl
#include "laserParticleCommon.hlsli"

Texture2D<float4> Snapshot : register(t0);
RWStructuredBuffer<LaserGpuParticle> Particles : register(u0);
RWByteAddressBuffer Counters : register(u1);

cbuffer LaserClearSpawnBuffer : register(b0)
{
    int2 rectOrigin;
    uint2 rectSize;
    uint mode;
    uint batchStart;
    uint reservedCount;
    uint capacityMask;
    uint maximumSeeds;
    uint seedBase;
    uint bornMilliseconds;
    uint minimumAlpha;
    float dpiScale;
    float dipPerPixel;
    float seedsPerSquareDip;
    uint durationMilliseconds;
    float4 front; // 方向 xy、最小投影、投影跨度倒数
    float2 wind;
    float windSpeedPxPerSecond;
    float meanderPx;
    float minimumWindSpeedFraction;
    float maximumWindSpeedFraction;
    float minimumRadiusDip;
    float maximumRadiusDip;
    float radiusDistributionExponent;
    float accelerationPerSecond;
    float gustVariation;
    float accelerationJitter;
    float minimumBurstDip;
    float maximumBurstDip;
    float edgeHalfWidth;
    float noiseAmplitude;
    uint minimumLifetimeMilliseconds;
    uint maximumLifetimeMilliseconds;
    uint2 padding;
};

uint StraightColor(uint4 rgba)
{
    // 纹理 Load 已按格式返回 RGBA；先还原字节，再照 CPU 的正数 lround 打包直通色。
    float3 scaled = saturate(float3(rgba.rgb) * (1.0 / float(rgba.a))) * 255.0;
    uint3 rgb = uint3(scaled) + uint3(frac(scaled) >= 0.5);
    return rgb.r | (rgb.g << 8) | (rgb.b << 16) | (rgba.a << 24);
}

LaserGpuParticle MakeClearParticle(uint2 pixel, uint random, uint4 rgba)
{
    LaserGpuParticle particle = (LaserGpuParticle)0;
    particle.position = float2(pixel) + float2(SampleParticleSeed(random ^ 0x967A889Bu),
        SampleParticleSeed(random ^ 0x02E5BE93u));
    float windSpeed = windSpeedPxPerSecond * lerp(minimumWindSpeedFraction, maximumWindSpeedFraction,
        SampleParticleSeed(random ^ 0xC2B2AE35u));
    particle.motion = wind * windSpeed;
    particle.shapeParameters.x = lerp(minimumRadiusDip, maximumRadiusDip,
        pow(SampleParticleSeed(random ^ 0xA511E9B3u), radiusDistributionExponent)) * dpiScale;
    particle.color = StraightColor(rgba);

    // 与 MakeDriftParticle 保持相同的坐标换算及速度归一化。
    float2 normal = float2(-wind.y, wind.x);
    float alongDip = dot(particle.position, wind) / dpiScale;
    float acrossDip = dot(particle.position, normal) / dpiScale;
    float gust = 1.0 + gustVariation * sin(0.019 * alongDip + 0.031 * acrossDip + 0.9) *
        cos(0.027 * acrossDip - 0.011 * alongDip - 0.4);
    float drag = accelerationPerSecond * gust * lerp(1.0 - accelerationJitter, 1.0 + accelerationJitter,
        SampleParticleSeed(random ^ 0xD3A2646Cu));
    float burstAngle = 6.28318530717958647692 * SampleParticleSeed(random ^ 0x85EBCA77u);
    float burstDistance = lerp(minimumBurstDip, maximumBurstDip, SampleParticleSeed(random ^ 0x165667B1u)) * dpiScale;
    particle.motionParameters = float4(max(drag, 0.0001), meanderPx,
        float2(cos(burstAngle), sin(burstAngle)) * burstDistance);
    float2 unitWind = particle.motion / max(length(particle.motion), 0.0001);
    float2 windPosition = float2(dot(particle.position, unitWind),
        dot(particle.position, float2(-unitWind.y, unitWind.x))) * dipPerPixel;
    particle.shapeParameters.zw = DriftWindField(windPosition);

    // 抖动后的浮点出生坐标可能恰好舍入到下一像素；遮罩仍按实际出生像素取值。
    float2 cell = floor(particle.position);
    float projection = dot(cell + 0.5, front.xy);
    float base = 1.0 - (projection - front.z) * front.w;
    float threshold = saturate(base + (ParticleCrumbleNoise(uint2(cell)) - 0.5) * noiseAmplitude);
    float center = edgeHalfWidth + (1.0 - 2.0 * edgeHalfWidth) * threshold;
    float releaseProgress = min(1.0, center + edgeHalfWidth);
    particle.releaseMilliseconds = bornMilliseconds + uint(ceil(releaseProgress * float(durationMilliseconds)));
    particle.lifetimeMilliseconds = uint(lerp(float(minimumLifetimeMilliseconds), float(maximumLifetimeMilliseconds),
        SampleParticleSeed(random ^ 0x27D4EB2Fu)));
    particle.kindAndBatch = LASER_PARTICLE_DRIFT;
    return particle;
}

[numthreads(8, 8, 1)]
void main(uint3 dispatchId : SV_DispatchThreadID, uint3 groupId : SV_GroupID, uint groupIndex : SV_GroupIndex)
{
    if (mode == 0u)
    {
        uint slot = groupId.x * 64u + groupIndex;
        if (slot < reservedCount)
            Particles[(batchStart + slot) & capacityMask] = (LaserGpuParticle)0;
        return;
    }

    if (any(dispatchId.xy >= rectSize))
        return;
    uint2 pixel = uint2(rectOrigin) + dispatchId.xy;
    uint4 rgba = uint4(round(Snapshot.Load(int3(pixel, 0)) * 255.0));
    if (rgba.a < minimumAlpha)
        return;

    uint index;
    if (mode == 1u)
    {
        Counters.InterlockedAdd(0, 1u, index);
        return;
    }

    uint inked = Counters.Load(0);
    if (inked == 0u)
        return;
    float wanted = min(float(maximumSeeds), float(inked) * seedsPerSquareDip / (dpiScale * dpiScale));
    uint threshold = uint(min(1.0, wanted / float(inked)) * 16777216.0);
    uint random = HashParticleSeed(ParticlePixelSeed(pixel) + seedBase);
    if ((random & 0x00FFFFFFu) >= threshold)
        return;
    Counters.InterlockedAdd(4, 1u, index);
    if (index < reservedCount)
        Particles[(batchStart + index) & capacityMask] = MakeClearParticle(pixel, random, rgba);
}
