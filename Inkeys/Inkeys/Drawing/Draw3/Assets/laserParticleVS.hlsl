// laserParticleVS.hlsl
#include "laserParticleCommon.hlsli"

// 画布粒子专用顶点着色器：每粒 4 个顶点（三角带），直接由出生参数和时钟求出当前状态。
// 没有计算着色器，也没有逐帧写回；过期、未释放和空槽输出退化图元。
StructuredBuffer<LaserGpuParticle> LaserParticles : register(t0);

cbuffer LaserParticleDrawBuffer : register(b0)
{
    float2 clipScale;             // (2 / 宽, -2 / 高)
    uint activeStart;             // 活跃环区间起点
    uint drawKind;                // 本次绘制的粒子种类
    uint nowMilliseconds;
    uint gatherMilliseconds;      // 汇聚时冻结的时钟
    uint gatherBatch;             // 正在汇聚的飘散批次，0 表示没有
    float gatherRemaining;        // 1 - 汇聚进度
    float shrinkStartTravelRatio;
    float trailEndRadiusScale;
    float driftEndRadiusScale;
    float inverseWhiteFadeSeconds;
    float dipPerPixel;
    float3 padding;
};

struct ParticleVertex
{
    float4 position : SV_POSITION;
    float2 offset : TEXCOORD0;               // 相对粒子中心的像素偏移
    nointerpolation float4 color : COLOR0;   // rgb 直通色，a 为整粒透明度
    nointerpolation float radius : TEXCOORD1;
};

// 风场只是位置的函数：相邻粒子读到相近的值，才会成缕飘走而不是各自乱晃。
// 坐标取“风向, 法向”基下的 DIP。CPU 侧在出生时用同一公式算好出生位置的值。
float2 DriftWindField(float2 p)
{
    return float2(
        sin(dot(p, float2(-0.01376, 0.03007))) + 0.6 * sin(dot(p, float2(0.04706, 0.05930)) + 1.7),
        sin(dot(p, float2(0.01200, 0.04325)) + 0.6));
}

ParticleVertex main(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    ParticleVertex output;
    output.position = float4(-2.0, -2.0, 0.0, 1.0);
    output.offset = 0.0;
    output.color = 0.0;
    output.radius = 0.0;

    LaserGpuParticle particle = LaserParticles[(activeStart + instanceId) & (LASER_PARTICLE_CAPACITY - 1u)];
    uint kind = particle.kindAndBatch & 3u;
    bool gathering = gatherBatch != 0u && (particle.kindAndBatch >> 2) == gatherBatch;
    // 无符号减法：尚未释放时结果极大，与过期、空槽（寿命为 0）一起被同一个比较剔除。
    uint elapsed = (gathering ? gatherMilliseconds : nowMilliseconds) - particle.releaseMilliseconds;
    if (kind != drawKind || elapsed >= particle.lifetimeMilliseconds)
        return output;

    float age = float(elapsed) * 0.001;
    float lifeFraction = float(elapsed) / float(particle.lifetimeMilliseconds);
    float4 color = float4(particle.color & 255u, (particle.color >> 8) & 255u,
        (particle.color >> 16) & 255u, particle.color >> 24) * (1.0 / 255.0);
    float2 center;
    float radius;
    float opacity;
    if (kind == LASER_PARTICLE_DRIFT)
    {
        // 从静止被风逐渐加速到终值；快慢按出生位置而异，线才会一段一段被吹散。
        float windSpeed = max(length(particle.motion), 1e-4);
        float2 wind = particle.motion / windSpeed;
        float2 normal = float2(-wind.y, wind.x);
        float drag = particle.motionParameters.x;
        float travel = windSpeed * (age - (1.0 - exp(-drag * age)) / drag);
        float2 advected = float2(dot(particle.position, wind) + travel, dot(particle.position, normal)) * dipPerPixel;
        // 减去出生处的场值：位移从零连续增长，轨迹是这片风场里的一条流线。
        float2 meander = (DriftWindField(advected) - particle.shapeParameters.zw) * particle.motionParameters.y;
        float2 displacement = wind * (travel + 0.32 * meander.y) + normal * meander.x +
            particle.motionParameters.zw * (1.0 - exp(-3.0 * age));
        // 汇聚：按冻结时钟求出的位移整体乘一个收回系数，沿原路退回出生位置。
        float remaining = gathering ? gatherRemaining : 1.0;
        center = particle.position + displacement * remaining;
        radius = particle.shapeParameters.x * lerp(1.0, driftEndRadiusScale, lifeFraction);
        opacity = (1.0 - lifeFraction * lifeFraction) * remaining;
        // 白芯处出生的粒子很快褪成来源色，粒子流里最终只有一种颜色。
        float whiteness = saturate(1.0 - age * inverseWhiteFadeSeconds);
        color.rgb = lerp(color.rgb, 1.0, 0.94 * saturate(particle.shapeParameters.y * whiteness * whiteness));
    }
    else
    {
        // 行进粒子：初速度乘 1 - smoothstep 的寿命减速曲线，这里用它的定积分，与帧率无关。
        float progress = lifeFraction - lifeFraction * lifeFraction * lifeFraction * (1.0 - 0.5 * lifeFraction);
        center = particle.position + particle.motion * progress;
        float shrink = smoothstep(shrinkStartTravelRatio, 1.0, saturate(progress * particle.shapeParameters.z));
        radius = particle.shapeParameters.x * lerp(1.0, trailEndRadiusScale, shrink);
        float ramp = smoothstep(0.0, 1.0, age * particle.motionParameters.w);
        // 亮度层次只乘透明度，颜色保持纯来源色。
        float brightness = saturate(particle.shapeParameters.y + particle.motionParameters.z *
            sin(particle.motionParameters.x * age + particle.motionParameters.y) * ramp);
        opacity = (1.0 - smoothstep(0.0, 1.0, lifeFraction)) * brightness;
    }

    float alpha = 0.96 * opacity * color.a;
    if (radius <= 0.0 || alpha <= 0.0)
        return output;

    // 抗锯齿过渡带半宽 0.625 像素，四边各留 1 像素就够。
    float2 corner = float2(vertexId & 1u, vertexId >> 1) * 2.0 - 1.0;
    float2 offset = corner * (radius + 1.0);
    output.position = float4((center + offset) * clipScale + float2(-1.0, 1.0), 0.0, 1.0);
    output.offset = offset;
    output.color = float4(color.rgb, alpha);
    output.radius = radius;
    return output;
}
