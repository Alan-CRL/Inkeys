#include "shape.hlsli"

ShapePixelInput main(uint vertex : SV_VertexID)
{
    ShapePixelInput result = (ShapePixelInput)0;
    float2 lo = shapeRect.xy, hi = shapeRect.zw;
    if (shapePass == 0)
    {
        uint offset = shapeBufferOffset + (vertex / 6) * 4;
        result.a = ShapeData[offset];
        result.b = ShapeData[offset + 1];
        result.c = ShapeData[offset + 2];
        result.d = ShapeData[offset + 3];
        // p0,p1,p2,p3 | pathStart,pathLength,halfWidth,radius | kind,flags,padding.
        uint kind = asuint(result.d.x);
        float2 p0 = result.a.xy, p1 = result.a.zw, p2 = result.b.xy;
        if (kind == 2)
        {
            float2 extent = sqrt(p1 * p1 + p2 * p2);
            lo = p0 - extent; hi = p0 + extent;
        }
        else if (kind == 3)
        {
            float2 axis = p2;
            float2 extent = abs(axis) * p1.x + abs(float2(-axis.y, axis.x)) * p1.y;
            lo = p0 - extent; hi = p0 + extent;
        }
        else
        {
            lo = min(p0, p1); hi = max(p0, p1);
            if (kind == 1) { lo = min(lo, p2); hi = max(hi, p2); }
        }
        float margin = (kind == 1 ? 0 : result.c.z) + 1;
        lo -= margin + shapeOrigin; hi += margin - shapeOrigin;
    }
    float2 pixel = lerp(lo, hi, ShapeCorner(vertex % 6));
    result.pixel = pixel;
    result.position = float4(pixel.x / shapeScreen.x * 2 - 1,
        1 - pixel.y / shapeScreen.y * 2, 0, 1);
    return result;
}
