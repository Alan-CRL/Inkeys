// laserParticleCommon.hlsli
#ifndef LASER_PARTICLE_COMMON_HLSLI
#define LASER_PARTICLE_COMMON_HLSLI

#define LASER_PARTICLE_CAPACITY 32768u
#define LASER_PARTICLE_DRIFT 2u

// Match C++ LaserGpuParticle (64 bytes).
// 粒子不保存逐帧状态：CPU 只在出生时写一次，之后的位置、大小、透明度都由顶点着色器按“出生参数 + 已过时间”求值。
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

#endif
