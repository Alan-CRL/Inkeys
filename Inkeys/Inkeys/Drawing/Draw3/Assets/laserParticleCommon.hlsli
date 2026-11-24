// laserParticleCommon.hlsli
#ifndef LASER_PARTICLE_COMMON_HLSLI
#define LASER_PARTICLE_COMMON_HLSLI

#define LASER_PARTICLE_DRIFT 2u

// Match C++ LaserGpuParticle (64 bytes).
// 出生参数只写一次；顶点着色器按“出生参数 + 已过时间”求值。
struct LaserGpuParticle
{
    float2 position;          // 出生位置（像素）
    // 行进粒子：初速度 × 寿命秒数；飘散粒子：风速终值向量。
    float2 motion;
    // 行进粒子：呼吸角频率、呼吸相位、呼吸幅度、呼吸渐入时长的倒数。
    // 飘散粒子：被风带起来的快慢、蜿蜒幅度、出生时散开的位移 xy。
    float4 motionParameters;
    // x 半径，y 亮度（行进：透明度层次；飘散：出生处的白芯比例）。
    // 行进粒子 z：总位移与收缩参考行程之比；飘散粒子 zw：出生位置的风场值。
    float4 shapeParameters;
    uint color;               // RGBA8，直通色
    uint lifetimeMilliseconds; // 0 表示空槽
    uint releaseMilliseconds;  // 粒子池时钟下的出生时刻（已含释放延时）
    uint kindAndBatch;         // 低 2 位种类，高位为激光飘散批次号（0 表示不参与汇聚）
};

uint HashParticleSeed(uint value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

float SampleParticleSeed(uint value)
{
    return float(HashParticleSeed(value) & 0x00FFFFFFu) * (1.0 / 16777216.0);
}

uint ParticlePixelSeed(uint2 pixel)
{
    return pixel.x ^ (pixel.y * 0x9E3779B9u);
}

float ParticleCrumbleNoise(uint2 pixel)
{
    // 与 inkPixelShader.hlsl 的逐像素碎裂遮罩同步：两个哈希的平均。
    uint hash = HashParticleSeed(ParticlePixelSeed(pixel));
    uint second = (hash ^ 0x68E31DA4u) * 0x9E3779B9u;
    second ^= second >> 16;
    return (float(hash & 0x00FFFFFFu) + float(second >> 8)) * (1.0 / 33554432.0);
}

// 风场只是位置的函数：相邻粒子读到相近的值，才会成缕飘走而不是各自乱晃。
// 坐标取“风向, 法向”基下的 DIP。CPU 侧在出生时用同一公式算好出生位置的值。
float2 DriftWindField(float2 p)
{
    return float2(
        sin(dot(p, float2(-0.01376, 0.03007))) + 0.6 * sin(dot(p, float2(0.04706, 0.05930)) + 1.7),
        sin(dot(p, float2(0.01200, 0.04325)) + 0.6));
}

#endif
