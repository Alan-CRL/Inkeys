module;

#include "../../../IdtMain.h"
#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>

export module Inkeys.UI.Bar:PenPreviewRendering;

import Inkeys.UI.UI3;

using namespace Inkeys::UI::UI3;

// 主栏专用预览自持缓存；通用绘制器只提供字体和设备绘制能力。
// 不绑定绘制器：需要绘制器能力的入口由调用者传入本次绘制所用的那一个。
export class BarPenPreviewRenderer
{
public:
	void ResetDeviceCaches();
	void ResetTypographyCaches() noexcept;
	struct ThicknessFineDialLabelCacheClass
	{
		int value = 0;
		FLOAT zoom = 0.0F;
		FLOAT fontSize = 0.0F;
		D2D1_SIZE_F size{};
		FLOAT layoutWidth = 0.0F;
		unsigned long long lastUse = 0;
		bool valid = false;
		ComPtr<IDWriteTextLayout> layout;
	};
	struct ThicknessPreviewGradientBrushCacheClass
	{
		COLORREF color = RGB(0, 0, 0);
		std::uint8_t leftOpacity = 0;
		std::uint64_t lastUse = 0;
		bool valid = false;
		ComPtr<ID2D1LinearGradientBrush> brush;
	};
	ID2D1LinearGradientBrush* GetThicknessPreviewGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT leftOpacity, FLOAT opacity);
	ID2D1PathGeometry* GetThicknessPreviewPath(UIRendering& renderer);
	ID2D1StrokeStyle1* GetThicknessPreviewStrokeStyle(UIRendering& renderer);
	ID2D1PathGeometry* GetThicknessFineDialSelectorGeometry(UIRendering& renderer);
	ThicknessFineDialLabelCacheClass* GetThicknessFineDialLabelLayout(
		UIRendering& renderer, int value, FLOAT zoom, ID2D1DeviceContext* context = nullptr);

private:
	UiFontCorrectionClass fineDialFontCorrection;
	bool thicknessPreviewGradientFailureLogged = false;
	bool thicknessPreviewGradientUnavailable = false;
	bool thicknessPreviewPathFailureLogged = false;
	bool thicknessPreviewPathUnavailable = false;
	bool thicknessFineDialSelectorUnavailable = false;
	bool thicknessFineDialSelectorFailureLogged = false;
	array<ThicknessPreviewGradientBrushCacheClass, 64>
		thicknessPreviewGradientBrushCache{};
	std::uint64_t thicknessPreviewGradientUseSerial = 0;
	ComPtr<ID2D1PathGeometry> thicknessPreviewPath;
	ComPtr<ID2D1StrokeStyle1> thicknessPreviewStrokeStyle;
	ComPtr<ID2D1PathGeometry> thicknessFineDialSelectorGeometry;
	array<ThicknessFineDialLabelCacheClass, 64>
		thicknessFineDialLabelCache{};
	unsigned long long thicknessFineDialLabelUseSerial = 0;
};
