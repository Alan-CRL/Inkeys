// laserParticleCommon.hlsli
#ifndef LASER_PARTICLE_COMMON_HLSLI
#define LASER_PARTICLE_COMMON_HLSLI

#define LASER_PARTICLE_CAPACITY 2048u

// 与 C++ LaserGpuParticle 保持 128 字节；状态只由 Compute Shader 更新。
struct LaserGpuParticle
{
    float2 position;
    float2 velocity;
    float ageSeconds;
    float lifetimeSeconds;
    float traveledDistance;
    float maximumTravelDistance;
    float baseRadius;
    float currentRadius;
    float opacity;
    float baseBrightness;
    float currentBrightness;
    float breathingFrequencyHz;
    float breathingPhase;
    float breathingAmplitude;
    float breathingRampSeconds;
    uint seed;
    uint alive;
    uint4 padding0;
    uint4 padding1;
    uint4 padding2;
    uint padding3;
};

uint LaserParticleHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

float LaserParticleRandom01(uint value)
{
    return (float) (LaserParticleHash(value) & 0x00FFFFFFu) / 16777216.0;
}

float2 LaserParticleSafeNormalize(float2 value, float2 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-5 ? value / valueLength : fallbackValue;
}

#ifdef LASER_PARTICLE_COMPUTE_RESOURCES
RWStructuredBuffer<LaserGpuParticle> LaserParticles : register(u0);
#endif

#endif
