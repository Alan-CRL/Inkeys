// 与普通 ImGui 绘制共用纹理和采样器；仅演示模式使用这些参数。
cbuffer EraserDemo : register(b1)
{
    float4 cursor; // x, y, fixed diameter, speed diameter
    float4 playback; // erase progress, cursor alpha, dark, source pixels per output pixel
    float4 speedPose; // independent speed cursor x, y, alpha
    float4 grip; // outline, stripe radius, stripe half height, stripe offset
    float4 material; // opacity, outline gray, demo mode
};
float2 AtlasUv(float2 pixel, float column)
{
    return (clamp(pixel,float2(0.5,0.5),float2(767.5,431.5)) + float2(column*768,0)) / float2(2304,432);
}
float Coverage(float distance)
{
    return saturate(0.5-distance/playback.w);
}
float Capsule(float2 p, float radius, float halfHeight)
{
    p.y -= clamp(p.y,-max(halfHeight-radius,0),max(halfHeight-radius,0));
    return length(p)-radius;
}
float4 RenderEraserDemo(float2 uv)
{
    float side = uv.x >= 0.5 ? 1 : 0;
    float2 pixel = float2(uv.x*2-side,uv.y)*float2(768,432);
    float4 ink = texture0.SampleLevel(sampler0,AtlasUv(pixel,0),0);
    float erased = 0;
    [branch] if (playback.x > 0)
    {
        float4 mask = texture0.SampleLevel(sampler0,AtlasUv(pixel,side+1),0);
        erased = playback.x >= 1 ? mask.b
            : mask.b*saturate((playback.x-mask.r)/max(mask.g-mask.r,1.0/255.0));
    }
    float3 background = playback.z > 0.5 ? float3(18,59,50)/255 : float3(247,248,250)/255;
    float3 color = ink.rgb;
    if (playback.z > 0.5)
    {
        color = ink.b > ink.r+0.15 ? float3(111,200,236)/255
            : (ink.r > ink.b+0.25 ? float3(242,192,104)/255 : float3(234,242,239)/255);
    }
    float3 result = lerp(background,color,ink.a*(1-erased));
    float diameter = side > 0.5 ? cursor.w : cursor.z;
    float cursorAlpha = side > 0.5 ? speedPose.z : playback.y;
    float2 local = pixel-(side > 0.5 ? speedPose.xy : cursor.xy);
    [branch] if (cursorAlpha > 0 && max(abs(local.x),abs(local.y)) < diameter*0.5+playback.w)
    {
        float distance = length(local)-diameter*0.5;
        float outer = Coverage(distance), inner = Coverage(distance+diameter*grip.x);
        float outlineAlpha = max(0,outer-inner)*material.x*cursorAlpha;
        float fillAlpha = inner*material.x*cursorAlpha;
        float alpha = outlineAlpha+fillAlpha;
        float3 rgb = alpha > 0 ? (outlineAlpha*material.y+fillAlpha)/alpha : 0;
        float stripe = min(inner,max(
            Coverage(Capsule(local-float2(-diameter*grip.w,0),diameter*grip.y,diameter*grip.z)),
            Coverage(Capsule(local-float2(diameter*grip.w,0),diameter*grip.y,diameter*grip.z))));
        rgb += stripe*(material.y-rgb);
        alpha += stripe*(material.x*cursorAlpha-alpha);
        result = rgb*alpha+result*(1-alpha);
    }
    return float4(result,1);
}
