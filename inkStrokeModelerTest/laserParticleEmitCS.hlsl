// laserParticleEmitCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleEmitBuffer : register(b0)
{
    uint4 laserEmitPath;
    float4 laserEmitArc;
    float4 laserEmitLifetimeTravel;
    float4 laserEmitRadiusSpeed0;
    float4 laserEmitSpeed1;
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
    float sampleRatio = ((float) localIndex + 0.5) / max((float) spawnCount, 1.0);
    float sampleArcLength = lerp(laserEmitArc.x, laserEmitArc.y, sampleRatio);
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
    float side = random0 < 0.5 ? -1.0 : 1.0;
    float coreRadius = pathRadius * laserEmitRadiusSpeed0.z;
    float startOffset = (random1 * 2.0 - 1.0) * coreRadius * 0.72;
    float signedExtra = side * random2 * max(laserEmitArc.w, 0.0);
    float speedJitter = lerp(0.88, 1.12, random3);
    float targetSpeed = clamp(laserEmitRadiusSpeed0.w +
        laserEmitSpeed1.y * max(header.filteredInputSpeed, 0.0),
        laserEmitRadiusSpeed0.w, laserEmitSpeed1.x) * speedJitter;

    LaserGpuParticle particle = (LaserGpuParticle) 0;
    particle.position = pathPosition +
        float2(-pathTangent.y, pathTangent.x) * startOffset;
    particle.tangent = pathTangent;
    particle.pathArcLength = min(sampleArcLength, pathEndArcLength);
    particle.birthArcLength = particle.pathArcLength;
    particle.maximumTravelDistance = lerp(
        laserEmitLifetimeTravel.z, laserEmitLifetimeTravel.w, random4);
    particle.flowSpeed = targetSpeed;
    particle.speedJitter = speedJitter;
    particle.lifetimeSeconds = lerp(
        laserEmitLifetimeTravel.x, laserEmitLifetimeTravel.y, random5);
    particle.lateralOffset = startOffset;
    particle.lateralStartOffset = startOffset;
    particle.lateralExtra = signedExtra;
    particle.pathRadius = pathRadius;
    particle.baseRadius = lerp(
        laserEmitRadiusSpeed0.x, laserEmitRadiusSpeed0.y,
        LaserParticleRandom01(seed ^ 0x85EBCA77u));
    particle.currentRadius = particle.baseRadius;
    particle.opacity = 1.0;
    particle.pathSlot = laserEmitPath.x;
    particle.pathGeneration = laserEmitPath.y;
    particle.segmentCursor = segmentCursor;
    particle.seed = seed;
    particle.alive = 1;
    particle.phase = 0;

    uint particleCapacity = min(laserEmitSeed.z, LASER_PARTICLE_CAPACITY);
    uint particleIndex = (laserEmitPath.z + localIndex) % max(particleCapacity, 1u);
    LaserParticles[particleIndex] = particle;
}
