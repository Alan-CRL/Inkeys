// laserParticleEmitCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleEmitBuffer : register(b0)
{
    uint4 laserEmitPath;
    float4 laserEmitSpawn;
    float4 laserEmitLifeBrightness;
    float4 laserEmitBreathRadius;
    float4 laserEmitRadiusSpeed;
    float4 laserEmitSpeed;
    uint4 laserEmitSeed;
};

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint localIndex = dispatchThreadId.x;
    uint spawnCount = min(laserEmitPath.w, LASER_PARTICLE_CAPACITY);
    if (localIndex >= spawnCount || laserEmitPath.x >= LASER_PARTICLE_PATH_CAPACITY)
        return;

    LaserParticlePathHeader header = LaserParticlePathHeaders[laserEmitPath.x];
    if (header.active == 0 || header.ended != 0 ||
        header.generation != laserEmitPath.y || header.pointCount == 0)
        return;

    uint seed = LaserParticleHash(laserEmitSeed.x + localIndex * 0x9E3779B9u);
    float sampleArcLength = max(laserEmitSpawn.x, 0.0);
    uint segmentCursor = 0;
    float2 pathPosition;
    float pathRadius;
    float2 pathTangent;
    float pathEndArcLength;
    if (!SampleLaserParticlePath(laserEmitPath.x, header, sampleArcLength,
        segmentCursor, float2(1.0, 0.0), pathPosition, pathRadius,
        pathTangent, pathEndArcLength))
        return;

    float random0 = LaserParticleRandom01(seed ^ 0x68BC21EBu);
    float random1 = LaserParticleRandom01(seed ^ 0x02E5BE93u);
    float random2 = LaserParticleRandom01(seed ^ 0x967A889Bu);
    float random3 = LaserParticleRandom01(seed ^ 0xC2B2AE35u);
    float random4 = LaserParticleRandom01(seed ^ 0x27D4EB2Fu);
    float random5 = LaserParticleRandom01(seed ^ 0x165667B1u);
    float random6 = LaserParticleRandom01(seed ^ 0xD3A2646Cu);
    float random7 = LaserParticleRandom01(seed ^ 0x85EBCA77u);
    float side = random0 < 0.5 ? -1.0 : 1.0;
    float coreRadius = pathRadius * laserEmitRadiusSpeed.y;
    float startOffset = (random1 * 2.0 - 1.0) * coreRadius * 0.72;
    float signedExtra = side * random2 * max(laserEmitSpawn.w, 0.0);
    float speedJitter = lerp(0.88, 1.12, random3);
    float targetSpeed = clamp(laserEmitRadiusSpeed.z +
        laserEmitSpeed.x * max(header.filteredInputSpeed, 0.0),
        laserEmitRadiusSpeed.z, laserEmitRadiusSpeed.w) * speedJitter;

    LaserGpuParticle particle = (LaserGpuParticle) 0;
    particle.position = laserEmitSpawn.yz +
        float2(-pathTangent.y, pathTangent.x) * startOffset;
    particle.tangent = pathTangent;
    particle.pathArcLength = min(sampleArcLength, pathEndArcLength);
    particle.endpointBlockedSeconds = 0.0;
    particle.flowSpeed = targetSpeed;
    particle.speedJitter = speedJitter;
    particle.lifetimeSeconds = laserEmitLifeBrightness.x;
    particle.lateralOffset = startOffset;
    particle.lateralStartOffset = startOffset;
    particle.lateralExtra = signedExtra;
    particle.pathRadius = pathRadius;
    particle.baseRadius = lerp(
        laserEmitBreathRadius.w, laserEmitRadiusSpeed.x, random7);
    particle.currentRadius = particle.baseRadius;
    particle.opacity = 1.0;
    particle.baseBrightness = lerp(
        laserEmitLifeBrightness.y, laserEmitLifeBrightness.z, random4);
    particle.currentBrightness = particle.baseBrightness;
    particle.breathingFrequencyHz = lerp(
        laserEmitBreathRadius.x, laserEmitBreathRadius.y, random5);
    particle.breathingPhase = random6 * 6.28318530718;
    particle.breathingAmplitude = laserEmitLifeBrightness.w;
    particle.breathingRampSeconds = laserEmitBreathRadius.z;
    particle.pathSlot = laserEmitPath.x;
    particle.pathGeneration = laserEmitPath.y;
    particle.segmentCursor = segmentCursor;
    particle.seed = seed;
    particle.alive = 1;

    uint particleCapacity = min(laserEmitSeed.z, LASER_PARTICLE_CAPACITY);
    uint particleIndex = (laserEmitPath.z + localIndex) % max(particleCapacity, 1u);
    LaserParticles[particleIndex] = particle;
}
