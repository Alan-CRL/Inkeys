// inkVertexShader.hlsl
#include "ink.hlsli"

static const float2 kQuadUVs[6] =
{
    float2(0, 0), float2(1, 0), float2(0, 1),
    float2(0, 1), float2(1, 0), float2(1, 1)
};

float2 SafeNormalize(float2 value, float2 fallbackValue)
{
    float valueLength = length(value);
    return valueLength > 1e-5 ? value / valueLength : fallbackValue;
}

float3 GetEraserRectSweepPlane(float2 segment, float2 half0, float2 half1,
    float2 sideNormal, float2 localMin)
{
    float2 cornerSign = float2(sideNormal.x >= 0.0 ? 1.0 : -1.0,
        sideNormal.y >= 0.0 ? 1.0 : -1.0);
    // 变尺寸可使支撑角点换象限；仅在 VS 枚举同号角点，不构造凸包数组。
    [unroll]
    for (int cornerIndex = 0; cornerIndex < 4; ++cornerIndex)
    {
        float2 candidateSign = cornerSign * float2(
            (cornerIndex & 1) == 0 ? 1.0 : -1.0,
            (cornerIndex & 2) == 0 ? 1.0 : -1.0);
        float2 point0 = half0 * candidateSign;
        float2 point1 = segment + half1 * candidateSign;
        float2 edge = point1 - point0;
        float2 normal = float2(-edge.y, edge.x);
        if (dot(normal, sideNormal) < 0.0) normal = -normal;
        float normalLength = length(normal);
        if (normalLength <= 1e-5 || dot(normal, sideNormal) <= 0.0) continue;
        normal /= normalLength;

        // 矩形支撑函数等价于检查全部八角点；容差以归一化后的像素距离计。
        float support0 = dot(abs(normal), half0);
        float support1 = dot(normal, segment) + dot(abs(normal), half1);
        if (max(support0, support1) <= dot(normal, point0) + 1e-4)
            return float3(normal, dot(normal, localMin - point0));
    }
    // 无效边恒为负，不参与 PS 的最大距离。
    return float3(0.0, 0.0, -1e20);
}

bool IsNormalPositiveRadius(float radius)
{
    // Check the IEEE-754 range without relying on NaN comparisons.
    uint bits = asuint(radius);
    return bits >= 0x00800000u && bits < 0x7f800000u;
}

bool IsValidRectSweepPoint(InkPoint sweepPoint)
{
    return IsNormalPositiveRadius(sweepPoint.r) && IsNormalPositiveRadius(sweepPoint.time)
        && all((asuint(sweepPoint.pos) & 0x7fffffffu) < 0x7f800000u);
}

PS_INPUT main(uint id : SV_VertexID, uint instanceId : SV_InstanceID)
{
    PS_INPUT output = (PS_INPUT) 0;
    int type = (int) (globalShapeType + 0.5);
    uint itemIndex = id / 6;
    uint vertexIndex = id % 6;
    float2 templatePos = kQuadUVs[vertexIndex];

    output.color = globalColor;
    output.shapeType = globalShapeType;

    if (type == 10)
    {
        // 点击光波：独立图元，不经过粒子池。每圈占 InkData 两个连续槽：
        // 第一槽为圆心、当前环半径、半线宽；第二槽为颜色 rgb 与整圈透明度。数值由 CPU 每帧按曲线求好。
        InkPoint ring = InkData[globalBufferOffset + itemIndex * 2];
        InkPoint tint = InkData[globalBufferOffset + itemIndex * 2 + 1];
        float ringHalfWidth = max(ring.time, 1e-4);
        float outerRadius = max(ring.r, 0.0) + ringHalfWidth;
        float2 center = ring.pos;
        float2 rectMin = center - outerRadius - 2.0;
        float2 rectMax = center + outerRadius + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        // p2.x 取负的半线宽，像素着色器据此只画半径 r1 处的一圈细线。
        output.p2 = float2(-ringHalfWidth, 1.0);
        output.r1 = ring.r;
        output.r2 = tint.time;
        output.color = float4(tint.pos, tint.r, 1.0);
        return output;
    }

    if (type == 9)
    {
        InkPoint dot = InkData[globalBufferOffset + itemIndex];
        float outerRadius = max(dot.r, 0.0) + laserRadii.z;
        float2 center = dot.pos;
        float2 rectMin = center - outerRadius - 2.0;
        float2 rectMax = center + outerRadius + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = outerRadius.xx;
        output.r1 = dot.r;
        output.r2 = dot.time;
        return output;
    }

    if (type == 24)
    {
        InkPoint cursor = InkData[globalBufferOffset];
        float2 center = cursor.pos;
        // 半宽高已包含 CPU 精灵的透明 AA 留白，不能再次外扩并拉伸 UV。
        float2 halfSize = float2(cursor.r, cursor.time);
        float2 worldPos = center + (templatePos * 2.0 - 1.0) * halfSize;
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = halfSize;
        output.uv = templatePos;
        return output;
    }

    if ((type >= 4 && type <= 6) || type == 21 || type == 22)
    {
        InkPoint cursor = InkData[globalBufferOffset];
        InkPoint style = InkData[globalBufferOffset + 1];
        float2 center = cursor.pos;
        float2 halfSize = float2(cursor.r, cursor.time);
        float2 rectMin = center - halfSize - 2.0;
        float2 rectMax = center + halfSize + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.p1 = center;
        output.p2 = halfSize;
        output.r1 = style.pos.x;
        output.r2 = style.pos.y;
        // x 编码独立边缘 opacity + 1；y 传递高亮光标的物理像素外黑边宽度。
        output.uv = float2(style.r, style.time);
        return output;
    }

    if (type == 23)
    {
        bool holeMode = globalPadding.y >= 0.5;
        uint segmentIndex = holeMode ? itemIndex / 4 : itemIndex;
        uint segmentOffset = globalBufferOffset + segmentIndex;
        // 矩形橡皮保留两端各自尺寸，包围盒不能使用平均半宽高。
        InkPoint start = InkData[segmentOffset];
        InkPoint end = InkData[segmentOffset + 1];
        float2 half0 = float2(start.r, start.time);
        float2 half1 = float2(end.r, end.time);
        float2 segment = end.pos - start.pos;
        bool zeroLength = dot(segment, segment) <= 1e-10;
        if (zeroLength) segment = 0.0;
        float2 localMin = min(-half0, segment - half1);
        float2 localMax = max(half0, segment + half1);
        float3 plane0 = float3(0.0, 0.0, -1e20);
        float3 plane1 = plane0;
        bool contained = all(abs(segment) + half0 <= half1) ||
            all(abs(segment) + half1 <= half0);
        // 零长或包含时只用单矩形 AABB，避免退化斜边影响覆盖。
        if (!zeroLength && !contained)
        {
            float2 sideNormal = float2(-segment.y, segment.x);
            plane0 = GetEraserRectSweepPlane(segment, half0, half1, sideNormal, localMin);
            plane1 = GetEraserRectSweepPlane(segment, half0, half1, -sideNormal, localMin);
        }
        // type 23 复用全为 nointerpolation 的字段：AABB、两个单位法线及偏移。
        output.p1 = start.pos + localMin;
        output.p2 = start.pos + localMax;
        output.color = float4(plane0.xy, plane1.xy);
        output.r1 = plane0.z;
        output.r2 = plane1.z;
        float2 rectMin = output.p1 - 2.0;
        float2 rectMax = output.p2 + 2.0;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        [branch] if (holeMode)
        {
            uint quad = itemIndex % 4;
            float2 holeHalf = half0 - 2.0;
            bool hasHole = false;
            // The first segment of every upload must draw its complete quad.
            [branch] if (segmentIndex >= 1 && all(holeHalf >= 4.0)
                && IsValidRectSweepPoint(start) && IsValidRectSweepPoint(end))
            {
                InkPoint previous = InkData[segmentOffset - 1];
                // Supporting planes and the zero-length/contained AABB both cover
                // the predecessor's end rect. Its own hole is covered by earlier segments.
                hasHole = IsValidRectSweepPoint(previous);
            }
            if (hasHole)
            {
                float2 holeMin = start.pos - holeHalf;
                float2 holeMax = start.pos + holeHalf;
                float2 bandMin = rectMin;
                float2 bandMax = rectMax;
                if (quad == 0) bandMax.y = holeMin.y;
                else if (quad == 1) bandMin.y = holeMax.y;
                else
                {
                    // Overlap the horizontal bands by one pixel at each T-junction.
                    bandMin.y = holeMin.y - 1.0;
                    bandMax.y = holeMax.y + 1.0;
                    if (quad == 2) bandMax.x = holeMin.x;
                    else bandMin.x = holeMax.x;
                }
                bandMin = clamp(bandMin, rectMin, rectMax);
                bandMax = clamp(bandMax, rectMin, rectMax);
                worldPos = all(bandMax > bandMin) ? lerp(bandMin, bandMax, templatePos) : start.pos;
            }
            else if (quad != 0)
            {
                worldPos = start.pos;
            }
        }
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        return output;
    }

    if (type == 3)
    {
        HighlighterPrimitive primitive = HighlighterData[globalBufferOffset + itemIndex];
        // 固定竖直矩形沿 p1→p2 扫掠，AABB 覆盖 sweep 的轴向边界。
        float2 rectMin = min(primitive.p1, primitive.p2) - primitive.halfSize - 2.0;
        float2 rectMax = max(primitive.p1, primitive.p2) + primitive.halfSize + 2.0;
        output.p1 = primitive.p1;
        output.p2 = primitive.p2;
        output.r1 = primitive.halfSize.x;
        output.r2 = primitive.halfSize.y;

        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        return output;
    }

    if (type == 8 || type == 11 || type == 12 || type == 13)
    {
        // Laser 矩形 pass 直接使用 b0，避免为两个角点上传和绑定 t0。
        float2 rectMin = globalColor.xy;
        float2 rectMax = globalColor.zw;
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = worldPos / float2(screenWidth, screenHeight);
        output.p1 = rectMin;
        output.p2 = rectMax;
        return output;
    }

    if (type == 14 || type == 15)
    {
        // Cache compose/apply 都由 b2 提供目标矩形和 tile-local 采样范围。
        float2 worldPos = lerp(historyTargetRect.xy, historyTargetRect.zw, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = lerp(historySourceUvRect.xy, historySourceUvRect.zw, templatePos);
        output.p1 = historyTargetRect.xy;
        output.p2 = historyTargetRect.zw;
        return output;
    }

    if (type == 20)
    {
        float2 worldPos = lerp(
            trustedSnapshotTargetRect.xy, trustedSnapshotTargetRect.zw, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = lerp(
            trustedSnapshotSourceUvRect.xy, trustedSnapshotSourceUvRect.zw, templatePos);
        output.p1 = trustedSnapshotTargetRect.xy;
        output.p2 = trustedSnapshotTargetRect.zw;
        return output;
    }

    if (type >= 16 && type <= 19)
    {
        uint primitiveOffset = globalBufferOffset + itemIndex * 2;
        ShapePrimitive primitive;
        primitive.start = InkData[primitiveOffset];
        primitive.end = InkData[primitiveOffset + 1];
        float2 p1 = primitive.start.pos;
        float2 p2 = primitive.end.pos;
        float halfWidth = max(primitive.start.r, 0.0);
        output.p1 = p1;
        output.p2 = p2;
        output.r1 = halfWidth;
        output.r2 = globalPadding.x;

        float2 worldPos = 0.0;
        if (type == 16 || type == 17)
        {
            float2 segment = p2 - p1;
            float segmentLength = length(segment);
            float2 tangent = segmentLength > 0.001
                ? segment / segmentLength : float2(1.0, 0.0);
            float2 normal = float2(-tangent.y, tangent.x);
            float extent = halfWidth + 2.0;
            float localX = lerp(-extent, segmentLength + extent, templatePos.x);
            float localY = lerp(-extent, extent, templatePos.y);
            worldPos = p1 + tangent * localX + normal * localY;
        }
        else
        {
            float2 rectMin = min(p1, p2);
            float2 rectMax = max(p1, p2);
            float extent = type == 18 ? halfWidth + 2.0 : 2.0;
            worldPos = lerp(rectMin - extent, rectMax + extent, templatePos);
            output.p1 = rectMin;
            output.p2 = rectMax;
        }

        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        return output;
    }

    uint stripCount = 1;
    uint quadInSegment = 0;
    if (type == 0 && globalPadding.y >= 2.0)
    {
        stripCount = (uint) globalPadding.y;
        quadInSegment = itemIndex % (2 * stripCount);
        itemIndex /= 2 * stripCount;
    }
    uint realIndex = globalBufferOffset + itemIndex;
    InkPoint data1 = InkData[realIndex];
    InkPoint data2 = InkData[realIndex + 1];
    float2 p1 = data1.pos;
    float2 p2 = data2.pos;
    float r1 = data1.r;
    float r2 = data2.r;

    if (type == 1 || type == 2 || type == 25)
    {
        float2 rectMin = min(p1, p2);
        float2 rectMax = max(p1, p2);
        float2 worldPos = lerp(rectMin, rectMax, templatePos);
        output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
            -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
        output.pixPos = worldPos;
        output.uv = worldPos / float2(screenWidth, screenHeight);
        output.p1 = rectMin;
        output.p2 = rectMax;
        return output;
    }

    // 普通笔、橡皮和 Laser 都使用端点半径构造足够覆盖完整材质的 OBB。
    float2 segment = p2 - p1;
    float segmentLength = length(segment);
    float2 tangent = segmentLength > 0.001 ? segment / segmentLength : float2(1.0, 0.0);
    float2 normal = float2(-tangent.y, tangent.x);
    float startRadius = type == 7 ? max(r1, 0.0) + laserRadii.z : r1;
    float endRadius = type == 7 ? max(r2, 0.0) + laserRadii.z : r2;
    float maxRadius = max(startRadius, endRadius);
    // The batch-wide K comes from the largest radius in the batch. A thinner segment in
    // the same batch uses fewer (or no) strips so that a strip never gets thinner than a
    // few pixels: sliver triangles perturb the interpolated pixPos and fwidth(d) enough to
    // change anti-aliased coverage visibly.
    // Tiers keep a strip between about 6 and 12 pixels tall, so the staircase around the
    // hole grows linearly with the radius. Must match the tiers in InkRenderer::DrawStroke.
    uint segmentStrips = maxRadius < 24.0 ? 1 : maxRadius < 48.0 ? 8 : maxRadius < 96.0 ? 16
        : maxRadius < 192.0 ? 32 : 64;
    segmentStrips = min(segmentStrips, stripCount);
    bool useStrips = segmentStrips >= 2 && segmentLength > 0.001 && abs(r1 - r2) < segmentLength
        && IsNormalPositiveRadius(r1) && IsNormalPositiveRadius(r2);
    float2 worldPos = p1;
    if (stripCount == 1 || (!useStrips && quadInSegment == 0))
    {
        float localX = lerp(-startRadius - 2.0, segmentLength + endRadius + 2.0, templatePos.x);
        float localY = lerp(-maxRadius - 2.0, maxRadius + 2.0, templatePos.y);
        worldPos = p1 + tangent * localX + normal * localY;
    }
    else if (useStrips && quadInSegment < 2 * segmentStrips)
    {
        uint strip = quadInSegment / 2;
        uint halfIndex = quadInSegment % 2;
        float extent = maxRadius + 2.0;
        float ya = max(-extent, lerp(-extent, extent, (float) strip / segmentStrips) - 0.5);
        float yb = min(extent, lerp(-extent, extent, (float) (strip + 1) / segmentStrips) + 0.5);
        float ymaxAbs = max(abs(ya), abs(yb));
        float yminAbs = (ya <= 0.0 && yb >= 0.0) ? 0.0 : min(abs(ya), abs(yb));
        float outer = sqrt(max(0.0, extent * extent - yminAbs * yminAbs));
        float xa = max(-outer, -startRadius - 2.0);
        float xb = min(segmentLength + outer, segmentLength + endRadius + 2.0);

        bool hasHole = false;
        float s = 0.0;
        // The first segment of each upload has no predecessor in this batch.
        [branch] if (itemIndex >= 1 && ymaxAbs < r1 - 2.0)
        {
            InkPoint data0 = InkData[realIndex - 1];
            // Below radius 1 the pixel shader blends in thin-line coverage, so the
            // previous segment is not guaranteed to have written full coverage.
            if (IsNormalPositiveRadius(data0.r) && data0.r >= 1.0)
            {
                float rho = r1 - 2.0;
                [branch] if (data0.r < rho)
                    rho = min(rho, length(p1 - data0.pos) + data0.r);
                if (rho > 0.0 && ymaxAbs < rho)
                {
                    s = sqrt(max(0.0, rho * rho - ymaxAbs * ymaxAbs));
                    hasHole = true;
                }
            }
        }
        if (hasHole)
        {
            if (halfIndex == 0) xb = min(xb, -s);
            else xa = max(xa, s);
        }
        else if (halfIndex != 0)
        {
            xb = xa;
        }
        if (xb > xa)
        {
            float localX = lerp(xa, xb, templatePos.x);
            float localY = lerp(ya, yb, templatePos.y);
            worldPos = p1 + tangent * localX + normal * localY;
        }
    }

    output.pos = float4((worldPos.x / screenWidth) * 2.0 - 1.0,
        -((worldPos.y / screenHeight) * 2.0 - 1.0), 0.0, 1.0);
    output.pixPos = worldPos;
    output.p1 = p1;
    output.p2 = p2;
    output.r1 = r1;
    output.r2 = r2;
    return output;
}
