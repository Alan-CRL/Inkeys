// laserParticleUpdateCS.hlsl
#define LASER_PARTICLE_COMPUTE_RESOURCES
#include "laserParticleCommon.hlsli"

cbuffer LaserParticleUpdateBuffer : register(b0)
{
    uint activeStart;
    uint activeCount;
    uint milliseconds;
    uint mode;
    float shrinkStartTravelRatio;
    float endRadiusScale;
    uint removedGroup;
    uint dissolveBirthMilliseconds;
    uint gatherMilliseconds;
    float gatherProgress;
    float dissolveEndRadiusScale;
    float dissolveWhiteFadeSeconds;
};

// 风场只是位置的函数：相邻粒子读到相近的值，才会成缕飘走而不是各自乱晃。
// 坐标取“风向, 法向”基下的 DIP。
float2 DissolveWindField(float2 p)
{
    return float2(
        sin(dot(p, float2(-0.01376, 0.03007))) + 0.6 * sin(dot(p, float2(0.04706, 0.05930)) + 1.7),
        sin(dot(p, float2(0.01200, 0.04325)) + 0.6));
}

[numthreads(64, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    if (dispatchThreadId.x >= activeCount)
        return;
    uint index = (activeStart + dispatchThreadId.x) % LASER_PARTICLE_CAPACITY;
    LaserGpuParticle particle = LaserParticles[index];
    if (particle.alive == 0)
        return;

    LaserParticleSeed source = particle.source;
    if (mode == 3u)
    {
        if (source.group == removedGroup)
        {
            particle.alive = 0;
            particle.opacity = 0.0;
            LaserParticles[index] = particle;
        }
        return;
    }
    bool dissolve = source.kind == LASER_PARTICLE_DISSOLVE;
    bool currentDissolve = dissolve && particle.birthMilliseconds == dissolveBirthMilliseconds;
    bool gathering = mode == 1u && currentDissolve;
    uint elapsed = (gathering ? gatherMilliseconds : milliseconds) - particle.birthMilliseconds;
    bool unreleased = elapsed < source.releaseDelayMilliseconds;
    elapsed -= source.releaseDelayMilliseconds;
    if (unreleased && !gathering && !(mode == 2u && currentDissolve))
        return;
    if (unreleased || elapsed >= source.lifetimeMilliseconds || (mode == 2u && currentDissolve))
    {
        particle.alive = 0;
        particle.opacity = 0.0;
        LaserParticles[index] = particle;
        return;
    }

    float age = float(elapsed) * 0.001;
    float lifeFraction = float(elapsed) / source.lifetimeMilliseconds;
    float2 displacement;
    if (dissolve)
    {
        // Evaluate at the frozen clock while gathering. Keeping the seed position
        // unchanged lets the return path use the same analytic motion.
        // 种子字段复用：velocity 为该粒子的风速终值，dragPerSecond 为它被风带起来的快慢，
        // breathingFrequencyHz 为像素到 DIP 的系数，breathingAmplitude 为蜿蜒幅度，
        // breathingPhase 与 angularSpeedRadians 为出生时散开的方向与距离，brightness 为出生处的白芯比例。
        float windSpeed = max(length(source.velocity), 1e-4);
        float2 wind = source.velocity / windSpeed;
        float2 normal = float2(-wind.y, wind.x);
        // 从静止被风逐渐加速到终值；快慢按出生位置而异，线才会一段一段被吹散。
        float drag = max(source.dragPerSecond, 1e-4);
        float travel = windSpeed * (age - (1.0 - exp(-drag * age)) / drag);
        float2 origin = float2(dot(source.position, wind), dot(source.position, normal)) * source.breathingFrequencyHz;
        float2 advected = origin + float2(travel * source.breathingFrequencyHz, 0.0);
        // 减去出生处的场值：位移从零连续增长，轨迹是这片风场里的一条流线。
        float2 meander = (DissolveWindField(advected) - DissolveWindField(origin)) * source.breathingAmplitude;
        float sine, cosine;
        sincos(source.breathingPhase, sine, cosine);
        displacement = wind * (travel + 0.32 * meander.y) + normal * meander.x +
            float2(cosine, sine) * (source.angularSpeedRadians * (1.0 - exp(-3.0 * age)));
        float remaining = gathering ? 1.0 - gatherProgress : 1.0;
        particle.position = source.position + displacement * remaining;
        particle.currentRadius = source.radius * lerp(1.0, dissolveEndRadiusScale, lifeFraction);
        particle.opacity = (1.0 - lifeFraction * lifeFraction) * remaining;
        // 白芯处出生的粒子很快褪成激光色，粒子流里最终只有一种颜色；VS 按这个比例混白。
        float whiteness = saturate(1.0 - age / max(dissolveWhiteFadeSeconds, 1e-4));
        particle.currentBrightness = source.brightness * whiteness * whiteness;
        LaserParticles[index] = particle;
        return;
    }
    if (source.kind == LASER_PARTICLE_CLICK_WAVE)
    {
        // 光波是一个环形图元：currentRadius 为环半径，currentBrightness 借作半线宽（像素）。
        float fade = 1.0 - lifeFraction;
        particle.currentRadius = source.radius + (1.0 - fade * fade * fade) * source.maximumTravelDistance;
        particle.currentBrightness = source.brightness * max(fade, 0.25);
        particle.opacity = 0.9 * fade * fade;
        LaserParticles[index] = particle;
        return;
    }
    // 行进粒子：初速度乘 1 - smoothstep 的寿命减速曲线，这里用它的定积分，与帧率无关。
    displacement = source.velocity * (source.lifetimeMilliseconds * 0.001) *
        (lifeFraction - lifeFraction * lifeFraction * lifeFraction * (1.0 - 0.5 * lifeFraction));
    particle.position = source.position + displacement;
    float travel = saturate(length(displacement) / max(source.maximumTravelDistance, 1e-4));
    float shrink = smoothstep(shrinkStartTravelRatio, 1.0, travel);
    particle.currentRadius = source.radius * lerp(1.0, endRadiusScale, shrink);
    float ramp = smoothstep(0.0, max(source.breathingRampSeconds, 1e-4), age);
    particle.currentBrightness = saturate(source.brightness + source.breathingAmplitude *
        sin(6.28318530718 * source.breathingFrequencyHz * age + source.breathingPhase) * ramp);
    particle.opacity = 1.0 - smoothstep(0.0, 1.0, lifeFraction);
    LaserParticles[index] = particle;
}
