// laserParticleCommon.hlsli
#ifndef LASER_PARTICLE_COMMON_HLSLI
#define LASER_PARTICLE_COMMON_HLSLI

#define LASER_PARTICLE_CAPACITY 2048u
#define LASER_PARTICLE_PATH_CAPACITY 32u
#define LASER_PARTICLE_PATH_POINT_CAPACITY 16384u

struct LaserParticlePathPoint
{
    float2 position;
    float radius;
    float arcLength;
};

struct LaserParticlePathHeader
{
    uint generation;
    uint pointCount;
    uint ended;
    uint active;
    float filteredInputSpeed;
    float3 padding;
};

// 与 C++ LaserGpuParticle 保持 128 字节；状态只由 Compute Shader 更新。
struct LaserGpuParticle
{
    float2 position;
    float2 tangent;
    float pathArcLength;
    float birthArcLength;
    float traveledDistance;
    float maximumTravelDistance;
    float flowSpeed;
    float speedJitter;
    float ageSeconds;
    float lifetimeSeconds;
    float lateralOffset;
    float lateralStartOffset;
    float lateralExtra;
    float pathRadius;
    float baseRadius;
    float currentRadius;
    float opacity;
    float convergeStartOpacity;
    float convergeStartRadius;
    float convergeStartOffset;
    float2 convergeStartPosition;
    uint pathSlot;
    uint pathGeneration;
    uint segmentCursor;
    uint seed;
    uint alive;
    uint phase;
    uint2 padding1;
};

uint LaserParticleHash(uint value)
{
    value ^= value >> 16;
    value *= 0x7FEB352Du;
    value ^= value >> 15;
    value *= 0x846CA68Bu;
    return value ^ (value >> 16);
}

float LaserParticleRandom01(uint value)
{
    return (float) (LaserParticleHash(value) & 0x00FFFFFFu) / 16777216.0;
}

float2 LaserParticleSafeNormalize(float2 value, float2 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-5 ? value / valueLength : fallbackValue;
}

bool LaserParticleConvergesToEdgeGpu(uint seed)
{
    return (LaserParticleHash(seed ^ 0xA511E9B3u) & 0xFFFFu) < 49152u;
}

#ifdef LASER_PARTICLE_COMPUTE_RESOURCES
StructuredBuffer<LaserParticlePathPoint> LaserParticlePathPoints : register(t0);
StructuredBuffer<LaserParticlePathHeader> LaserParticlePathHeaders : register(t1);
RWStructuredBuffer<LaserGpuParticle> LaserParticles : register(u0);

// 段游标命中时为 O(1)，路径追加或游标失配时使用固定 15 步二分定位。
bool SampleLaserParticlePath(uint pathSlot, LaserParticlePathHeader header,
    float requestedArcLength, inout uint segmentCursor, float2 fallbackTangent,
    out float2 position, out float radius, out float2 tangent, out float pathEndArcLength)
{
    position = 0.0;
    radius = 0.0;
    tangent = fallbackTangent;
    pathEndArcLength = 0.0;
    uint pointCount = min(header.pointCount, LASER_PARTICLE_PATH_POINT_CAPACITY);
    if (pathSlot >= LASER_PARTICLE_PATH_CAPACITY || pointCount == 0)
        return false;

    uint baseIndex = pathSlot * LASER_PARTICLE_PATH_POINT_CAPACITY;
    LaserParticlePathPoint firstPoint = LaserParticlePathPoints[baseIndex];
    LaserParticlePathPoint lastPoint = LaserParticlePathPoints[baseIndex + pointCount - 1];
    pathEndArcLength = max(lastPoint.arcLength, firstPoint.arcLength);
    float targetArcLength = clamp(requestedArcLength,
        firstPoint.arcLength, pathEndArcLength);

    if (pointCount == 1)
    {
        segmentCursor = 0;
        position = firstPoint.position;
        radius = firstPoint.radius;
        tangent = LaserParticleSafeNormalize(fallbackTangent, float2(1.0, 0.0));
        return true;
    }

    segmentCursor = min(segmentCursor, pointCount - 2);
    LaserParticlePathPoint point0 = LaserParticlePathPoints[baseIndex + segmentCursor];
    LaserParticlePathPoint point1 = LaserParticlePathPoints[baseIndex + segmentCursor + 1];
    bool cursorContainsTarget = targetArcLength >= point0.arcLength &&
        targetArcLength <= point1.arcLength;
    if (!cursorContainsTarget)
    {
        uint low = 0;
        uint high = pointCount - 1;
        [unroll]
        for (uint iteration = 0; iteration < 15; ++iteration)
        {
            if (low + 1 < high)
            {
                uint middle = (low + high) >> 1;
                if (LaserParticlePathPoints[baseIndex + middle].arcLength <= targetArcLength)
                    low = middle;
                else
                    high = middle;
            }
        }
        segmentCursor = min(low, pointCount - 2);
        point0 = LaserParticlePathPoints[baseIndex + segmentCursor];
        point1 = LaserParticlePathPoints[baseIndex + segmentCursor + 1];
    }

    // 重复点不产生新切线；短距离向前寻找后仍无有效段时沿用上一切线。
    [unroll]
    for (uint skip = 0; skip < 8; ++skip)
    {
        if (point1.arcLength - point0.arcLength > 1e-5 ||
            segmentCursor + 1 >= pointCount - 1)
            break;
        ++segmentCursor;
        point0 = LaserParticlePathPoints[baseIndex + segmentCursor];
        point1 = LaserParticlePathPoints[baseIndex + segmentCursor + 1];
    }

    float arcSpan = point1.arcLength - point0.arcLength;
    float interpolation = arcSpan > 1e-5
        ? saturate((targetArcLength - point0.arcLength) / arcSpan) : 0.0;
    position = lerp(point0.position, point1.position, interpolation);
    radius = max(lerp(point0.radius, point1.radius, interpolation), 0.0);
    tangent = LaserParticleSafeNormalize(
        point1.position - point0.position,
        LaserParticleSafeNormalize(fallbackTangent, float2(1.0, 0.0)));
    return true;
}
#endif

#endif
