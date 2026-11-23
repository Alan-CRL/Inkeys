// laserParticleEmitCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

StructuredBuffer<LaserParticleSeed> LaserParticleSeeds : register(t0);

cbuffer LaserParticleEmitBuffer : register(b0)
{
    uint spawnStart;
    uint spawnCount;
    uint milliseconds;
    uint padding;
};

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint index = dispatchThreadId.x;
    if (index >= spawnCount)
        return;

    LaserParticleSeed source = LaserParticleSeeds[index];
    LaserGpuParticle particle;
    particle.source = source;
    particle.position = source.position;
    particle.currentRadius = source.radius;
    particle.opacity = source.releaseDelayMilliseconds == 0 && source.lifetimeMilliseconds != 0 ? 1.0 : 0.0;
    particle.currentBrightness = source.brightness;
    particle.birthMilliseconds = milliseconds;
    particle.alive = source.lifetimeMilliseconds != 0 ? 1u : 0u;
    LaserParticles[(spawnStart + index) % LASER_PARTICLE_CAPACITY] = particle;
}
