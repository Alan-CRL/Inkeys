// Research only: two production function bodies below are extracted verbatim.
// Fake D2D context counts API calls; no HWND, COM, GPU or actual draw occurs.
#include <algorithm>
#include <cmath>
#include <cassert>
#include <iostream>
#include <array>
using std::clamp; using std::min; using std::max; using std::abs;
using FLOAT=float; using UINT32=unsigned int;
struct D2D1_RECT_F { FLOAT left,top,right,bottom; };
struct D2D1_SIZE_F { FLOAT width,height; };
struct D2D1_ROUNDED_RECT { D2D1_RECT_F rect; FLOAT radiusX,radiusY; };
namespace D2D1 { inline D2D1_RECT_F RectF(FLOAT l,FLOAT t,FLOAT r,FLOAT b) { return {l,t,r,b}; } }
enum D2D1_ANTIALIAS_MODE { D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1_ANTIALIAS_MODE_ALIASED };
struct ID2D1Bitmap {};
struct ID2D1Bitmap1: ID2D1Bitmap {};
struct ID2D1Brush {};
struct ID2D1RadialGradientBrush: ID2D1Brush { void SetOpacity(FLOAT) {} };
struct BitmapRef { ID2D1Bitmap1* value=nullptr; explicit operator bool() const { return value!=nullptr; } ID2D1Bitmap1* Get() const { return value; } };
struct ID2D1DeviceContext {
    unsigned long calls=0;
    D2D1_ANTIALIAS_MODE mode=D2D1_ANTIALIAS_MODE_PER_PRIMITIVE;
    D2D1_ANTIALIAS_MODE GetAntialiasMode() const { return mode; }
    void SetAntialiasMode(D2D1_ANTIALIAS_MODE value) { mode=value; }
    void FillOpacityMask(ID2D1Bitmap*,ID2D1Brush*,const D2D1_RECT_F* dst,const D2D1_RECT_F*) {
        assert(mode==D2D1_ANTIALIAS_MODE_ALIASED);
        assert(dst->right>dst->left && dst->bottom>dst->top);
        ++calls;
    }
};
class BarUIRendering {
public:
    struct FrameDiffuseMaskCacheClass { FLOAT padding=0,radiusX=0,radiusY=0; D2D1_SIZE_F size{}; BitmapRef bitmap; };
    struct Key { UINT32 width=0,height=0; };
    struct Ready { Key key; BitmapRef bitmap; };
    struct FrameDiffuseExactMaskSelectionClass { const Ready* cache=nullptr; D2D1_RECT_F destination{}; };
    unsigned int FillRoundedRectDiffuseMaskSlices(ID2D1DeviceContext*,ID2D1Bitmap*,ID2D1Brush*,const FLOAT*,const FLOAT*,const FLOAT*,const FLOAT*,int);
    void DrawRoundedRectDiffuseMask(ID2D1DeviceContext*,const FrameDiffuseMaskCacheClass&,const FrameDiffuseExactMaskSelectionClass&,const D2D1_ROUNDED_RECT&,ID2D1RadialGradientBrush*,FLOAT);
};

#include <cstdint>
namespace Inkeys::UI::RenderPipeline {
struct FrameDiagnostics { struct Light { std::uint64_t slices=0; } light; };
FrameDiagnostics* current=nullptr;
FrameDiagnostics* CurrentFrameDiagnostics() noexcept { return current; }
}

// Current production function SHA256: b7c68979f63eca1e7a655bc8ee552268232e44be751c098a04b61ea7461a6dde
unsigned int BarUIRendering::FillRoundedRectDiffuseMaskSlices(
	ID2D1DeviceContext* deviceContext, ID2D1Bitmap* bitmap,
	ID2D1Brush* brush, const FLOAT* sourceX, const FLOAT* sourceY,
	const FLOAT* destinationX, const FLOAT* destinationY,
	int segmentCount)
{
	if (!deviceContext || !bitmap || !brush || !sourceX || !sourceY
		|| !destinationX || !destinationY || segmentCount <= 0) return 0;
	unsigned int fillCount = 0;
	for (int y = 0; y < segmentCount; y++)
	{
		for (int x = 0; x < segmentCount; x++)
		{
			if (destinationX[x + 1] <= destinationX[x]
				|| destinationY[y + 1] <= destinationY[y]) continue;
			D2D1_RECT_F destinationRect = D2D1::RectF(
				destinationX[x], destinationY[y],
				destinationX[x + 1], destinationY[y + 1]);
			D2D1_RECT_F sourceRect = D2D1::RectF(
				sourceX[x], sourceY[y],
				sourceX[x + 1], sourceY[y + 1]);
			deviceContext->FillOpacityMask(
				bitmap, brush, &destinationRect, &sourceRect);
			++fillCount;
		}
	}
	// 聚合实际提交数，包含整图烘焙；零面积跳片不计入，也不逐片访问采样槽。
	if (auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics())
		diagnostics->light.slices += fillCount;
	return fillCount;
}

void BarUIRendering::DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
	const FrameDiffuseMaskCacheClass& mask,
	const FrameDiffuseExactMaskSelectionClass& exactMask,
	const D2D1_ROUNDED_RECT& roundedRect,
	ID2D1RadialGradientBrush* brush, FLOAT opacity)
{
	if (!deviceContext || !mask.bitmap || !brush || opacity <= 0.0F) return;
	brush->SetOpacity(clamp(opacity, 0.0F, 1.0F));

	FLOAT destinationLeft = roundedRect.rect.left - mask.padding;
	FLOAT destinationTop = roundedRect.rect.top - mask.padding;
	FLOAT destinationRight = roundedRect.rect.right + mask.padding;
	FLOAT destinationBottom = roundedRect.rect.bottom + mask.padding;
	FLOAT destinationRadiusX = max(0.0F, roundedRect.radiusX);
	FLOAT destinationRadiusY = max(0.0F, roundedRect.radiusY);
	FLOAT destinationMiddleLeft = min(
		roundedRect.rect.left + destinationRadiusX,
		(roundedRect.rect.left + roundedRect.rect.right) * 0.5F);
	FLOAT destinationMiddleRight = max(
		roundedRect.rect.right - destinationRadiusX, destinationMiddleLeft);
	FLOAT destinationMiddleTop = min(
		roundedRect.rect.top + destinationRadiusY,
		(roundedRect.rect.top + roundedRect.rect.bottom) * 0.5F);
	FLOAT destinationMiddleBottom = max(
		roundedRect.rect.bottom - destinationRadiusY, destinationMiddleTop);

	D2D1_ANTIALIAS_MODE originalAntialiasMode = deviceContext->GetAntialiasMode();
	deviceContext->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
	if (exactMask.cache && exactMask.cache->bitmap)
	{
		D2D1_RECT_F sourceRect = D2D1::RectF(
			0.0F, 0.0F,
			static_cast<FLOAT>(exactMask.cache->key.width),
			static_cast<FLOAT>(exactMask.cache->key.height));
		deviceContext->FillOpacityMask(
			exactMask.cache->bitmap.Get(), brush,
			&exactMask.destination, &sourceRect);
		if (auto* diagnostics = Inkeys::UI::RenderPipeline::CurrentFrameDiagnostics())
			++diagnostics->light.slices;
		deviceContext->SetAntialiasMode(originalAntialiasMode);
		return;
	}
	bool geometryScaled = abs(destinationRadiusX - mask.radiusX) > 0.001F
		|| abs(destinationRadiusY - mask.radiusY) > 0.001F;
	if (!geometryScaled)
	{
		const FLOAT sourceX[] = { 0.0F,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft,
			destinationMiddleLeft, destinationMiddleRight, destinationRight };
		const FLOAT destinationY[] = { destinationTop,
			destinationMiddleTop, destinationMiddleBottom, destinationBottom };
		FillRoundedRectDiffuseMaskSlices(
			deviceContext, mask.bitmap.Get(), brush,
			sourceX, sourceY, destinationX, destinationY, 3);
	}
	else
	{
		// 动画缩放时单独保留 Gaussian 外扩段，避免柔光宽度随圆角一起压扁。
		const FLOAT sourceX[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusX,
			mask.padding + mask.radiusX + 1.0F,
			mask.padding + mask.radiusX * 2.0F + 1.0F, mask.size.width };
		const FLOAT sourceY[] = { 0.0F, mask.padding,
			mask.padding + mask.radiusY,
			mask.padding + mask.radiusY + 1.0F,
			mask.padding + mask.radiusY * 2.0F + 1.0F, mask.size.height };
		const FLOAT destinationX[] = { destinationLeft, roundedRect.rect.left,
			destinationMiddleLeft, destinationMiddleRight,
			roundedRect.rect.right, destinationRight };
		const FLOAT destinationY[] = { destinationTop, roundedRect.rect.top,
			destinationMiddleTop, destinationMiddleBottom,
			roundedRect.rect.bottom, destinationBottom };
		FillRoundedRectDiffuseMaskSlices(
			deviceContext, mask.bitmap.Get(), brush,
			sourceX, sourceY, destinationX, destinationY, 5);
	}
	deviceContext->SetAntialiasMode(originalAntialiasMode);
}

int main() {
    BarUIRendering renderer; ID2D1Bitmap1 bitmap; ID2D1RadialGradientBrush brush;
    for (bool enabled: {false,true}) for (const FLOAT zoom: {1.0F,1.3F}) {
        Inkeys::UI::RenderPipeline::FrameDiagnostics sample;
        Inkeys::UI::RenderPipeline::current=enabled?&sample:nullptr;
        const FLOAT radius=4.0F*zoom;
        const FLOAT cachedRadius=static_cast<FLOAT>(std::lround(static_cast<double>(radius)*4.0))/4.0F;
        const FLOAT cachedStroke=static_cast<FLOAT>(std::lround(static_cast<double>(zoom)*4.0))/4.0F;
        const FLOAT padding=std::ceil(cachedStroke*3.0F+cachedStroke*0.5F+1.0F);
        BarUIRendering::FrameDiffuseMaskCacheClass mask;
        mask.padding=padding; mask.radiusX=mask.radiusY=cachedRadius;
        mask.size={padding*2.0F+cachedRadius*2.0F+1.0F,padding*2.0F+cachedRadius*2.0F+1.0F};
        mask.bitmap.value=&bitmap;
        ID2D1DeviceContext context;
        for (int i=0;i<15;++i) {
            const FLOAT x=i<11?5.0F+35.0F*static_cast<FLOAT>(i/2):250.0F;
            const FLOAT y=i<11?(i%2==0?5.0F:40.0F):40.0F+35.0F*static_cast<FLOAT>(i-11);
            const FLOAT w=i<11?30.0F:115.0F;
            const D2D1_ROUNDED_RECT rounded{{x*zoom,y*zoom,(x+w)*zoom,(y+30.0F)*zoom},radius,radius};
            renderer.DrawRoundedRectDiffuseMask(&context,mask,{},rounded,&brush,1.0F);
            assert(context.mode==D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        }
        const unsigned long expected=zoom==1.0F?135UL:375UL;
        assert(context.calls==expected);
        assert(sample.light.slices==(enabled?expected:0));
        const D2D1_ROUNDED_RECT shape{{0,0,30,30},4,4};
        BarUIRendering::Ready ready; ready.key={40,40}; ready.bitmap.value=&bitmap;
        BarUIRendering::FrameDiffuseExactMaskSelectionClass exact{&ready,{-5,-5,35,35}};
        const auto before=context.calls;
        renderer.DrawRoundedRectDiffuseMask(&context,mask,exact,shape,&brush,1.0F);
        assert(context.calls==before+1);
        assert(sample.light.slices==(enabled?context.calls:0));
        assert(context.mode==D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        renderer.DrawRoundedRectDiffuseMask(&context,mask,{},shape,&brush,0.0F);
        renderer.DrawRoundedRectDiffuseMask(nullptr,mask,{},shape,&brush,1.0F);
        assert(context.calls==before+1);
        assert(sample.light.slices==(enabled?context.calls:0));
        const FLOAT zero[]{0,0,0,0};
        assert(renderer.FillRoundedRectDiffuseMaskSlices(&context,&bitmap,&brush,zero,zero,zero,zero,3)==0);
        assert(sample.light.slices==(enabled?context.calls:0));
        std::cout << "actual bodies: counters=" << enabled << ", zoom=" << zoom << ", rounded_shapes=15 + exact=1, total_FillOpacityMask_calls=" << context.calls << '\n';
    }
}
