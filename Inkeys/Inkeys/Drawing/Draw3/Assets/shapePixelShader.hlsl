#include "shape.hlsli"

struct ShapeOutput { float4 add : SV_Target0; float4 retain : SV_Target1; };

// Rounded dash caps are evaluated from path length; no CPU short-dash geometry.
float DashDistance(float path, float width)
{
    if (shapeDashKind == 0) return 0;
    float unit = max(width, 0.0001);
    float period = shapeDashKind == 1 ? 10 : (shapeDashKind == 2 ? 3 : 14);
    float x = (path + shapeDashOffset) / unit;
    x -= floor(x / period) * period;
    float end = shapeDashKind == 2 ? 0 : 4;
    float gap = min(max(x - end, 0), period - x);
    if (shapeDashKind == 3) gap = min(gap, abs(x - 9));
    return gap * unit;
}

ShapeOutput main(ShapePixelInput input)
{
    ShapeOutput output;
    if (shapePass != 0)
    {
        int3 location = int3(input.position.xy, 0);
        output.add = ShapeAdd.Load(location);
        output.retain = ShapeRetain.Load(location).rrrr;
        return output;
    }
    // 使用真实像素中心，避免插值 TEXCOORD 在整画布与 tile 的投影间产生舍入差异。
    float2 samplePosition = input.position.xy + shapeOrigin;
    float2 p0 = input.a.xy, p1 = input.a.zw, p2 = input.b.xy;
    float halfWidth = input.c.z;
    uint kind = asuint(input.d.x), flags = asuint(input.d.y);
    float distance = 0;
    if (kind == 0)
    {
        float2 delta = p1 - p0;
        float segmentLength = sqrt(dot(delta, delta));
        float2 tangent = segmentLength > 0.00001 ? delta / segmentLength : float2(1, 0);
        float2 relative = samplePosition - p0;
        float projection = dot(relative, tangent);
        float clamped = clamp(projection, 0, segmentLength);
        float crossDistance = abs(ShapeCross(relative, tangent));
        float endpoint = abs(projection - clamped);
        // 箭头头部与箭杆共用覆盖批次，但头部始终为实线。
        float gap = (flags & 16) != 0 ? 0 : DashDistance(input.c.x + clamped, halfWidth * 2);
        distance = length(float2(max(endpoint, gap), crossDistance)) - halfWidth;
    }
    else if (kind == 1)
    {
        float orientation = ShapeCross(p1 - p0, p2 - p0) < 0 ? -1 : 1;
        float3 edges = orientation * float3(
            ShapeCross(p1 - p0, samplePosition - p0) / max(length(p1 - p0), 0.00001),
            ShapeCross(p2 - p1, samplePosition - p1) / max(length(p2 - p1), 0.00001),
            ShapeCross(p0 - p2, samplePosition - p2) / max(length(p0 - p2), 0.00001));
        // Internal triangulation edges use a hard partition, preserving full coverage at seams.
        if (((flags & 2) == 0 && edges.x < 0) || ((flags & 4) == 0 && edges.y < 0) ||
            ((flags & 8) == 0 && edges.z < 0)) discard;
        distance = -min((flags & 2) != 0 ? edges.x : 1e10,
            min((flags & 4) != 0 ? edges.y : 1e10, (flags & 8) != 0 ? edges.z : 1e10));
    }
    else if (kind == 2)
    {
        float2 relative = samplePosition - p0;
        float2 radii = float2(length(p1), length(p2));
        float2 local = float2(dot(relative, p1) / max(radii.x, 0.00001),
            dot(relative, p2) / max(radii.y, 0.00001));
        float k0 = length(local / max(radii, 0.00001));
        float k1 = length(local / max(radii * radii, 0.00001));
        distance = k1 > 0.00001 ? k0 * (k0 - 1) / k1 : -min(radii.x, radii.y);
        if ((flags & 1) == 0) distance = abs(distance) - halfWidth;
    }
    else if (kind == 3)
    {
        float2 relative = samplePosition - p0;
        float2 local = float2(dot(relative, p2), ShapeCross(p2, relative));
        float radius = clamp(input.c.w, 0, min(p1.x, p1.y));
        float2 q = abs(local) - p1 + radius;
        distance = length(max(q, 0)) + min(max(q.x, q.y), 0) - radius;
        if ((flags & 1) == 0) distance = abs(distance) - halfWidth;
    }
    else discard;
    float coverage = saturate(0.5 - distance);
    float alpha = shapeColor.a * coverage;
    output.add = float4(shapeColor.rgb * alpha, alpha);
    output.retain = (1 - alpha).xxxx;
    return output;
}
