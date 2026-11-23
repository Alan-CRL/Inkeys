// laserParticleCommon.hlsli
#ifndef LASER_PARTICLE_COMMON_HLSLI
#define LASER_PARTICLE_COMMON_HLSLI

#define LASER_PARTICLE_CAPACITY 32768u
#define LASER_PARTICLE_CLICK_WAVE 1u
#define LASER_PARTICLE_DISSOLVE 2u

// Match C++ LaserParticleSeed (88 bytes) and LaserGpuParticle (116 bytes).
struct LaserParticleSeed
{
    float2 position;
    float2 velocity;
    float4 color;
    float radius;
    float brightness;
    float breathingFrequencyHz;
    float breathingPhase;
    float dragPerSecond;
    float angularSpeedRadians;
    float maximumTravelDistance;
    float breathingAmplitude;
    float breathingRampSeconds;
    uint lifetimeMilliseconds;
    uint releaseDelayMilliseconds;
    uint randomSeed;
    uint group;
    uint kind;
};

struct LaserGpuParticle
{
    LaserParticleSeed source;
    float2 position;
    float currentRadius;
    float opacity;
    float currentBrightness;
    uint birthMilliseconds;
    uint alive;
};

#ifdef LASER_PARTICLE_COMPUTE_RESOURCES
RWStructuredBuffer<LaserGpuParticle> LaserParticles : register(u0);
#endif

#endif
