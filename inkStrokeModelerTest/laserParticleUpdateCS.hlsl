// laserParticleUpdateCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleUpdateBuffer : register(b0)
{
    float4 laserUpdateTime;
    float4 laserUpdateSpeed;
    float4 laserUpdateFade;
    uint4 laserUpdateFlags;
};

void KillLaserParticle(inout LaserGpuParticle particle)
{
    particle.alive = 0;
    particle.opacity = 0.0;
    particle.currentRadius = 0.0;
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint particleIndex = dispatchThreadId.x;
    if (particleIndex >= min(laserUpdateFlags.z, LASER_PARTICLE_CAPACITY))
        return;

    LaserGpuParticle particle = LaserParticles[particleIndex];
    if (laserUpdateFlags.x != 0)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }
    if (particle.alive == 0)
        return;

    if (particle.pathSlot >= LASER_PARTICLE_PATH_CAPACITY)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    LaserParticlePathHeader header = LaserParticlePathHeaders[particle.pathSlot];
    if (header.active == 0 || header.generation != particle.pathGeneration)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    float wallDeltaSeconds = max(laserUpdateTime.x, 0.0);
    float motionDeltaSeconds = clamp(laserUpdateTime.y, 0.0, 1.0 / 30.0);
    float2 pathPosition;
    float pathRadius;
    float2 pathTangent;
    float pathEndArcLength;

    if (header.ended != 0 && particle.phase == 0)
    {
        particle.phase = 1;
        particle.ageSeconds = 0.0;
        particle.convergeStartOpacity = particle.opacity;
        particle.convergeStartRadius = particle.currentRadius;
        particle.convergeStartOffset = particle.lateralOffset;
        particle.convergeStartPosition = particle.position;
    }

    if (particle.phase != 0)
    {
        particle.ageSeconds += wallDeltaSeconds;
        if (!SampleLaserParticlePath(particle.pathSlot, header,
            particle.pathArcLength, particle.segmentCursor, particle.tangent,
            pathPosition, pathRadius, pathTangent, pathEndArcLength))
        {
            KillLaserParticle(particle);
            LaserParticles[particleIndex] = particle;
            return;
        }

        float convergence = saturate(particle.ageSeconds /
            max(laserUpdateFade.y, 1e-4));
        float eased = convergence * convergence * (3.0 - 2.0 * convergence);
        float side = particle.lateralExtra < 0.0 ? -1.0 : 1.0;
        float targetOffset = LaserParticleConvergesToEdgeGpu(particle.seed)
            ? side * pathRadius : 0.0;
        float2 targetPosition = pathPosition +
            float2(-pathTangent.y, pathTangent.x) * targetOffset;
        particle.lateralOffset = lerp(
            particle.convergeStartOffset, targetOffset, eased);
        particle.position = lerp(
            particle.convergeStartPosition, targetPosition, eased);
        particle.tangent = pathTangent;
        particle.pathRadius = pathRadius;
        particle.currentRadius = particle.convergeStartRadius * (1.0 - eased);
        particle.opacity = particle.convergeStartOpacity * (1.0 - eased);
        if (convergence >= 0.9999)
            KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    particle.ageSeconds += wallDeltaSeconds;
    if (particle.ageSeconds >= particle.lifetimeSeconds)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    if (!SampleLaserParticlePath(particle.pathSlot, header,
        particle.pathArcLength, particle.segmentCursor, particle.tangent,
        pathPosition, pathRadius, pathTangent, pathEndArcLength))
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    float targetSpeed = clamp(laserUpdateSpeed.x +
        laserUpdateSpeed.z * max(header.filteredInputSpeed, 0.0),
        laserUpdateSpeed.x, laserUpdateSpeed.y) * particle.speedJitter;
    float speedBlend = 1.0 - exp(-motionDeltaSeconds /
        max(laserUpdateTime.w, 1e-4));
    particle.flowSpeed = lerp(particle.flowSpeed, targetSpeed, speedBlend);

    float remainingTravel = max(
        particle.maximumTravelDistance - particle.traveledDistance, 0.0);
    float requestedAdvance = min(
        max(particle.flowSpeed, 0.0) * motionDeltaSeconds, remainingTravel);
    float advancedArcLength = min(
        particle.pathArcLength + requestedAdvance, pathEndArcLength);
    float actualAdvance = max(advancedArcLength - particle.pathArcLength, 0.0);
    particle.pathArcLength = advancedArcLength;
    particle.traveledDistance += actualAdvance;

    if (!SampleLaserParticlePath(particle.pathSlot, header,
        particle.pathArcLength, particle.segmentCursor, particle.tangent,
        pathPosition, pathRadius, pathTangent, pathEndArcLength))
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    float responseBlend = 1.0 - exp(-motionDeltaSeconds /
        max(laserUpdateFade.z, 1e-4));
    particle.tangent = LaserParticleSafeNormalize(
        lerp(particle.tangent, pathTangent, responseBlend), pathTangent);
    particle.pathRadius = pathRadius;
    float spread = saturate(particle.ageSeconds /
        max(laserUpdateSpeed.w, 1e-4));
    float spreadEased = spread * spread * (3.0 - 2.0 * spread);
    float side = particle.lateralExtra < 0.0 ? -1.0 : 1.0;
    float targetOffset = side * (pathRadius + abs(particle.lateralExtra));
    particle.lateralOffset = lerp(
        particle.lateralStartOffset, targetOffset, spreadEased);
    float2 targetPosition = pathPosition +
        float2(-particle.tangent.y, particle.tangent.x) * particle.lateralOffset;
    particle.position = lerp(particle.position, targetPosition, responseBlend);

    float lifeFraction = particle.ageSeconds / max(particle.lifetimeSeconds, 1e-4);
    float fade = 1.0 - smoothstep(laserUpdateFade.x, 1.0, lifeFraction);
    particle.opacity = saturate(fade);
    particle.currentRadius = particle.baseRadius;
    LaserParticles[particleIndex] = particle;
}
