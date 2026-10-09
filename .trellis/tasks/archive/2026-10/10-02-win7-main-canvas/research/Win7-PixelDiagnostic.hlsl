// Diagnostic only: never replaces production SHADER resources 301-304.
cbuffer ScreenBuffer : register(b0)
{
    float screenWidth;
    float screenHeight;
    float globalShapeType;
    uint globalBufferOffset;
    float4 globalColor;
    uint globalOperatorKind;
    float3 globalPadding;
};

struct PS_INPUT
{
    float4 pos : SV_POSITION;
    float2 pixPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    nointerpolation float4 color : COLOR;
    nointerpolation float2 p1 : VAL_START;
    nointerpolation float2 p2 : VAL_END;
    nointerpolation float r1 : VAL_RAD_START;
    nointerpolation float r2 : VAL_RAD_END;
    nointerpolation float shapeType : VAL_TYPE;
};

struct OperatorOutput
{
    float4 add : SV_Target0;
    float4 retain : SV_Target1;
};

OperatorOutput ConstantOutput()
{
    OperatorOutput output;
    output.add = 0.5;
    output.retain = 0.5;
    return output;
}

bool Near(float actual, float expected)
{
    // Exponent check survives /O3 and rejects NaN/Inf without relying on isnan.
    return (asuint(actual) & 0x7f800000u) != 0x7f800000u && abs(actual - expected) <= 0.001;
}
bool Near2(float2 a, float2 b) { return Near(a.x, b.x) && Near(a.y, b.y); }
bool Near4(float4 a, float4 b) { return Near2(a.xy, b.xy) && Near2(a.zw, b.zw); }

OperatorOutput ValidateFields(PS_INPUT input)
{
    uint red = 0, green = 0, blue = 0;
    if (!Near(input.shapeType, 4)) red |= 1;
    if (!Near4(input.color, float4(1, 1, 1, 0.5))) red |= 2;
    if (!Near(input.r1, 0.5)) red |= 4;
    if (!Near(input.r2, 1)) red |= 8;
    if (!Near2(input.p1, float2(64, 64))) green |= 1;
    if (!Near2(input.p2, float2(16, 16))) green |= 2;
    if (!Near2(input.pixPos, input.pos.xy)) green |= 4;
    if (!Near2(input.uv, float2(0, 0))) green |= 8;
    if (!Near2(input.pos.zw, float2(0, 1))) green |= 16;
    if (!Near(screenWidth, 128) || !Near(screenHeight, 128)) blue |= 1;
    if (!Near(globalShapeType, 4) || globalBufferOffset != 0) blue |= 2;
    if (!Near4(globalColor, float4(1, 1, 1, 0.5))) blue |= 4;
    if (globalOperatorKind != 0) blue |= 8;
    if (!Near2(globalPadding.xy, (184.0 / 255.0).xx) ||
        !Near(globalPadding.z, 184.0 / 255.0)) blue |= 16;
    OperatorOutput output;
    // Opaque black means every field passed; RGB encodes mismatch bit groups.
    output.add = float4(float3(red, green, blue) / 255.0, 1);
    output.retain = 0;
    return output;
}

OperatorOutput CursorOnly(PS_INPUT input)
{
    // Same production shape=4 circle equations, without unrelated tool branches.
    float distanceToShape = length(input.pixPos - input.p1) - min(input.p2.x, input.p2.y);
    float outerCoverage = saturate(0.5 - distanceToShape);
    float innerCoverage = saturate(0.5 - (distanceToShape + max(input.r1, 0.0)));
    float outlineAlpha = max(0.0, outerCoverage - innerCoverage) * saturate(input.color.a);
    float fillAlpha = innerCoverage * saturate(input.r2) * saturate(input.color.a);
    float alpha = outlineAlpha + fillAlpha;
    float3 rgb = 0;
    if (alpha > 0)
        rgb = (outlineAlpha * saturate(globalPadding) + fillAlpha * saturate(input.color.rgb)) / alpha;
    if (alpha <= 0) discard;
    OperatorOutput output;
    output.add = float4(rgb * alpha, alpha);
    output.retain = (1.0 - alpha).xxxx;
    return output;
}


OperatorOutput RouteSentinel(float red, float green)
{
    OperatorOutput output;
    output.add = float4(red, green, 0.5, 0.5);
    output.retain = 0.5;
    return output;
}

OperatorOutput CompactIntegerRoute(PS_INPUT input)
{
    // Force actual integer conversion and nested dynamic gates, not a float-only shortcut.
    int type = (int)(input.shapeType + 0.5);
    [branch] if (type >= 4 && type <= 6)
    {
        [branch] if (type == 4) return RouteSentinel(0.5, 0.5);
        return RouteSentinel(0.5, 0);
    }
    return RouteSentinel(0, 0.5);
}

OperatorOutput CompactFloatRoute(PS_INPUT input)
{
    float type = input.shapeType;
    [branch] if (type >= 4 && type <= 6)
    {
        [branch] if (type == 4) return RouteSentinel(0.5, 0.5);
        return RouteSentinel(0.5, 0);
    }
    return RouteSentinel(0, 0.5);
}
