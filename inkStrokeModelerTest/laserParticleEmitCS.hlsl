// laserParticleEmitCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleEmitBuffer : register(b0)
{
    uint4 laserEmitSpawn;
    float4 laserEmitSource;
    float4 laserEmitLife;
    float4 laserEmitMotionRadius;
    float4 laserEmitVisual;
    float4 laserEmitBreath;
};

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localIndex = dispatchThreadId.x;
    uint spawnCount = min(laserEmitSpawn.y, LASER_PARTICLE_CAPACITY);
    if (localIndex >= spawnCount)
        return;

    uint seed = LaserParticleHash(
        laserEmitSpawn.w + localIndex * 0x9E3779B9u);
    float random0 = LaserParticleRandom01(seed ^ 0x68BC21EBu);
    float random1 = LaserParticleRandom01(seed ^ 0x02E5BE93u);
    float random2 = LaserParticleRandom01(seed ^ 0x967A889Bu);
    float random3 = LaserParticleRandom01(seed ^ 0xC2B2AE35u);
    float random4 = LaserParticleRandom01(seed ^ 0x27D4EB2Fu);
    float random5 = LaserParticleRandom01(seed ^ 0x165667B1u);
    float random6 = LaserParticleRandom01(seed ^ 0xD3A2646Cu);
    float random7 = LaserParticleRandom01(seed ^ 0x85EBCA77u);
    float random8 = LaserParticleRandom01(seed ^ 0xA511E9B3u);

    float2 tangent = LaserParticleSafeNormalize(
        laserEmitSource.zw, float2(1.0, 0.0));
    float side = random0 < 0.5 ? -1.0 : 1.0;
    float2 selectedNormal = float2(-tangent.y, tangent.x) * side;
    float deflection = lerp(-laserEmitMotionRadius.z,
        laserEmitMotionRadius.z, random1);
    float2 direction = LaserParticleSafeNormalize(
        selectedNormal * cos(deflection) + tangent * sin(deflection),
        selectedNormal);

    float entityCoreRadius = max(laserEmitLife.x, 0.0) *
        saturate(laserEmitLife.y);
    float coreOffset = random2 * entityCoreRadius * 0.72;
    float launchSpeed = lerp(
        laserEmitMotionRadius.x, laserEmitMotionRadius.y, random3);
    float lifetimeSeconds = lerp(
        laserEmitLife.z, laserEmitLife.w, random4);

    LaserGpuParticle particle = (LaserGpuParticle) 0;
    particle.position = laserEmitSource.xy + selectedNormal * coreOffset;
    particle.velocity = direction * launchSpeed;
    particle.lifetimeSeconds = lifetimeSeconds;
    particle.maximumTravelDistance = max(
        launchSpeed * lifetimeSeconds * 0.5, 1e-4);
    particle.baseRadius = lerp(
        laserEmitMotionRadius.w, laserEmitVisual.x, random8);
    particle.currentRadius = particle.baseRadius;
    particle.opacity = 1.0;
    particle.baseBrightness = lerp(
        laserEmitVisual.y, laserEmitVisual.z, random5);
    particle.currentBrightness = particle.baseBrightness;
    particle.breathingFrequencyHz = lerp(
        laserEmitBreath.x, laserEmitBreath.y, random6);
    particle.breathingPhase = random7 * 6.28318530718;
    particle.breathingAmplitude = laserEmitVisual.w;
    particle.breathingRampSeconds = laserEmitBreath.z;
    particle.seed = seed;
    particle.alive = 1;

    uint particleCapacity = min(
        laserEmitSpawn.z, LASER_PARTICLE_CAPACITY);
    uint particleIndex = (laserEmitSpawn.x + localIndex) %
        max(particleCapacity, 1u);
    LaserParticles[particleIndex] = particle;
}
