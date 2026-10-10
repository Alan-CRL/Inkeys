// laserParticleUpdateCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleUpdateBuffer : register(b0)
{
    float4 laserUpdateParameters;
    uint2 laserUpdateFlags;
    float2 laserUpdatePadding;
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
    if (particleIndex >= min(laserUpdateFlags.y, LASER_PARTICLE_CAPACITY))
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

    float wallDeltaSeconds = max(laserUpdateParameters.x, 0.0);
    float motionDeltaSeconds = clamp(
        laserUpdateParameters.y, 0.0, 1.0 / 30.0);
    particle.ageSeconds += wallDeltaSeconds;
    float lifetimeSeconds = max(particle.lifetimeSeconds, 1e-4);
    if (particle.ageSeconds >= lifetimeSeconds)
    {
        KillLaserParticle(particle);
        LaserParticles[particleIndex] = particle;
        return;
    }

    float lifeFraction = saturate(particle.ageSeconds / lifetimeSeconds);
    float lifeFactor = 1.0 - smoothstep(0.0, 1.0, lifeFraction);
    // 出生后不再读取轨迹；固定初速度只乘生命周期减速曲线。
    float2 motion = particle.velocity * lifeFactor * motionDeltaSeconds;
    particle.position += motion;
    particle.traveledDistance += length(motion);

    float maximumTravelDistance = max(
        particle.maximumTravelDistance, 1e-4);
    float travelRatio = saturate(
        particle.traveledDistance / maximumTravelDistance);
    float shrinkStart = clamp(laserUpdateParameters.z, 0.0, 0.9999);
    float shrinkRatio = saturate(
        (travelRatio - shrinkStart) / max(1.0 - shrinkStart, 1e-4));
    float shrinkSmooth = smoothstep(0.0, 1.0, shrinkRatio);
    particle.currentRadius = particle.baseRadius * lerp(
        1.0, saturate(laserUpdateParameters.w), shrinkSmooth);

    float breathingRampRatio = saturate(particle.ageSeconds /
        max(particle.breathingRampSeconds, 1e-4));
    float breathingRamp = breathingRampRatio * breathingRampRatio *
        (3.0 - 2.0 * breathingRampRatio);
    particle.currentBrightness = saturate(particle.baseBrightness +
        particle.breathingAmplitude * sin(6.28318530718 *
        particle.breathingFrequencyHz * particle.ageSeconds +
        particle.breathingPhase) * breathingRamp);
    // 呼吸只改变 RGB 亮度，Alpha 始终使用同一生命周期曲线。
    particle.opacity = lifeFactor;
    LaserParticles[particleIndex] = particle;
}
