// laserParticleUpdateCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleUpdateBuffer : register(b0)
{
    float4 laserUpdateTime;
    float4 laserUpdateSpeed;
    float4 laserUpdatePosition;
    uint4 laserUpdateFlags;
};

void KillLaserParticle(inout LaserGpuParticle particle)
{
    particle.alive = 0;
    particle.opacity = 0.0;
    particle.currentRadius = 0.0;
    particle.currentBrightness = 0.0;
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
    particle.ageSeconds += wallDeltaSeconds;
    float lifetimeSeconds = max(particle.lifetimeSeconds, laserUpdateSpeed.w);
    if (particle.ageSeconds >= lifetimeSeconds)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    float2 pathPosition;
    float pathRadius;
    float2 pathTangent;
    float pathEndArcLength;

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
        max(laserUpdateTime.z, 1e-4));
    particle.flowSpeed = lerp(particle.flowSpeed, targetSpeed, speedBlend);

    float lifeFraction = saturate(particle.ageSeconds / lifetimeSeconds);
    float lifeFactor = 1.0 - smoothstep(0.0, 1.0, lifeFraction);
    float requestedAdvance = max(particle.flowSpeed, 0.0) *
        lifeFactor * motionDeltaSeconds;
    float advancedArcLength = min(
        particle.pathArcLength + requestedAdvance, pathEndArcLength);
    particle.pathArcLength = advancedArcLength;

    if (!SampleLaserParticlePath(particle.pathSlot, header,
        particle.pathArcLength, particle.segmentCursor, particle.tangent,
        pathPosition, pathRadius, pathTangent, pathEndArcLength))
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    particle.pathRadius = pathRadius;
    float spread = saturate(particle.ageSeconds /
        max(laserUpdateTime.w, 1e-4));
    float spreadEased = spread * spread * (3.0 - 2.0 * spread);
    float side = particle.lateralExtra < 0.0 ? -1.0 : 1.0;
    float targetOffset = side * (pathRadius + abs(particle.lateralExtra));
    particle.lateralOffset = lerp(
        particle.lateralStartOffset, targetOffset, spreadEased);
    float2 targetPosition = pathPosition +
        float2(-pathTangent.y, pathTangent.x) * particle.lateralOffset;
    float directFollowDistance = max(laserUpdatePosition.z,
        2.0 * max(header.filteredInputSpeed, 0.0) * motionDeltaSeconds +
        2.0 * laserUpdatePosition.w);
    float targetDistance = distance(particle.position, targetPosition);
    bool requiresLimitedCorrection =
        particle.predictionCorrectionActive > 0.5 ||
        targetDistance > directFollowDistance;
    float normalResponseBlend = 1.0 - exp(-motionDeltaSeconds /
        max(laserUpdatePosition.x, 1e-4));
    float maximumCorrectionDistance = max(particle.flowSpeed, 0.0) *
        lifeFactor * max(laserUpdatePosition.y, 0.0) * motionDeltaSeconds;
    float tangentBlend = requiresLimitedCorrection && targetDistance > 1e-4
        ? saturate(maximumCorrectionDistance / targetDistance)
        : normalResponseBlend;
    particle.tangent = LaserParticleSafeNormalize(
        lerp(particle.tangent, pathTangent, tangentBlend), pathTangent);
    targetPosition = pathPosition +
        float2(-particle.tangent.y, particle.tangent.x) * particle.lateralOffset;
    if (requiresLimitedCorrection)
    {
        // prediction 跳变或回缩时，最多按当前实际沿线速度的固定倍率追赶。
        float2 correction = targetPosition - particle.position;
        float correctionDistance = length(correction);
        if (correctionDistance <= maximumCorrectionDistance ||
            correctionDistance <= 1e-4)
        {
            particle.position = targetPosition;
            particle.predictionCorrectionActive = 0.0;
        }
        else
        {
            particle.position += correction *
                saturate(maximumCorrectionDistance / correctionDistance);
            particle.predictionCorrectionActive = 1.0;
        }
    }
    else
    {
        particle.position = lerp(
            particle.position, targetPosition, normalResponseBlend);
        particle.predictionCorrectionActive = 0.0;
    }

    float breathingRampRatio = saturate(particle.ageSeconds /
        max(particle.breathingRampSeconds, 1e-4));
    float breathingRamp = breathingRampRatio * breathingRampRatio *
        (3.0 - 2.0 * breathingRampRatio);
    particle.currentBrightness = saturate(particle.baseBrightness +
        particle.breathingAmplitude * sin(6.28318530718 *
        particle.breathingFrequencyHz * particle.ageSeconds +
        particle.breathingPhase) * breathingRamp);
    particle.opacity = lifeFactor;
    particle.currentRadius = particle.baseRadius;
    LaserParticles[particleIndex] = particle;
}
