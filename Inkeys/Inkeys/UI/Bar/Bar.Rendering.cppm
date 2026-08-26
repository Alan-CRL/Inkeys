module;

#include "../../../IdtMain.h"

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>

export module Inkeys.UI.Bar:Rendering;

import :UI;
import :State;
import :Button;
import :Format;
import :RenderingAttribute;

import Inkeys.UI.Bar.Animation;
import Inkeys.UI.RenderPipeline;
using Ui3RenderDeviceEpoch = Inkeys::UI::RenderPipeline::DeviceEpoch;

constexpr double BarSvgRasterUpscaleThreshold = 1.35;
constexpr double BarSvgRasterSizeEpsilon = 0.01;
constexpr double BarBorderLightRadius = 480.0;
constexpr double BarBorderCursorFadeInDur = 0.30;
constexpr double BarBorderCursorLightRadius = 240.0;
constexpr double BarBorderLightIntensity = 1.0;
constexpr double BarBorderFrameDiffuseOpacity = 0.30;
constexpr double BarBorderPenDiffuseOpacity = 0.20;
// 标准差等于线宽时，1px 线源经过一维 Gaussian 后中心约保留 38.3%。
constexpr double BarBorderGaussianCenterCoverage = 0.382924922548;

class BarUISetClass;
class BarRenderLoopCoordinator;

enum class BarBorderLightSourceEnum : int
{
	Primary,
	Cursor,
};
enum class BarBorderPrimaryAnchorEnum : int
{
	MainButton,
	Select,
	Draw,
	Eraser,
	Geometry,
};

// Surface 只消费 Main Bar 已经计算完成的两路光源，不复制第三光源状态机。
export struct BarUiFrameLightingSnapshot
{
	D2D1_POINT_2F primaryLight = D2D1::Point2F();
	FLOAT primaryRadius = 0.0F;
	D2D1_POINT_2F cursorLight = D2D1::Point2F();
	D2D1_POINT_2F cursorScreenLight = D2D1::Point2F();
	FLOAT cursorRadius = 0.0F;
	FLOAT cursorIntensity = 0.0F;
	COLORREF drawingPenColor = RGB(0, 0, 0);
	double drawingPenColorBlend = 0.0;
	double drawingLightOpacity = 1.0;
	bool primaryLightVisible = false;
	bool cursorLightVisible = false;
	bool edgeLightingEnabled = false;
};

// 具体渲染
class BarUIRendering
{
public:
	BarUIRendering() {};
	BarUIRendering(BarUISetClass* barUISetClassT);

public:
	bool Shape(ID2D1DeviceContext* deviceContext, const BarUiShapeClass& shape, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Superellipse(ID2D1DeviceContext* deviceContext, const BarUiSuperellipseClass& superellipse, const BarUiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Svg(ID2D1DeviceContext* deviceContext, BarUiSVGClass& svg, const BarUiInheritClass& inh);
	bool Png(ID2D1DeviceContext* deviceContext, BarUiPNGClass& png, const BarUiInheritClass& inh);
	bool Word(ID2D1DeviceContext* deviceContext, const BarUiWordClass& word, const BarUiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER);
	D2D1_SIZE_F MeasureText(const wstring& content, double fontSize,
		DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL);
	bool PrepareFrameLighting(double animationDtSeconds,
		int drawingMode, int penMode, COLORREF brush1Color,
		COLORREF highlighterColor);
	[[nodiscard]] RECT GetFramePrimaryLightDamageBounds() const noexcept;
	[[nodiscard]] RECT GetFrameCursorLightDamageBounds() const noexcept;
	[[nodiscard]] bool DidFramePrimaryLightChange() const noexcept
	{
		return framePrimaryLightChanged;
	}
	[[nodiscard]] bool DidFrameCursorLightChange() const noexcept
	{
		return frameCursorLightChanged;
	}
	[[nodiscard]] BarUiFrameLightingSnapshot
		SnapshotFrameLighting() const noexcept;
	void SetFrameLightingSnapshot(
		const BarUiFrameLightingSnapshot& snapshot) noexcept;
	void SetFrameZoom(double zoom)
	{
		frameZoom = std::isfinite(zoom) && zoom > 0.0 ? zoom : 1.0;
	}
	[[nodiscard]] double GetFrameZoom() const noexcept
	{
		return frameZoom;
	}
	HRESULT EnsureDeviceResources(const Ui3RenderDeviceEpoch& epoch,
		UINT32 targetWidth, UINT32 targetHeight);
	[[nodiscard]] ID2D1DeviceContext* GetDeviceContext() const noexcept
	{
		return deviceContext.Get();
	}
	[[nodiscard]] ID2D1GdiInteropRenderTarget* GetGdiInteropRenderTarget() const noexcept
	{
		return gdiInteropRenderTarget.Get();
	}
	[[nodiscard]] unsigned long long GetDeviceGeneration() const noexcept
	{
		return deviceGeneration;
	}
	[[nodiscard]] D2D1_SIZE_U GetTargetBitmapSize() const noexcept
	{
		return D2D1::SizeU(targetBitmapWidth, targetBitmapHeight);
	}
	void DiscardDeviceResources();
	void PushFrameDirtyClip(
		ID2D1DeviceContext* deviceContext, const D2D1_RECT_F& dirtyRect);
	void PopFrameDirtyClip(ID2D1DeviceContext* deviceContext);
	void HandleFrameEndDrawResult(HRESULT endDrawResult);
	void SetFrameDiffuseMaskGeometryScale(double scale)
	{
		frameDiffuseMaskGeometryScale = scale > 0.0 ? scale : 1.0;
	}
	void SetFrameCursorLightLocalGeometry(
		D2D1_POINT_2F center, D2D1_SIZE_F radius) noexcept
	{
		frameLocalCursorLight = center;
		frameLocalCursorLightRadiusX =
			std::isfinite(radius.width) && radius.width > 0.0F
				? radius.width : 0.0F;
		frameLocalCursorLightRadiusY =
			std::isfinite(radius.height) && radius.height > 0.0F
				? radius.height : 0.0F;
	}

public:
	BarUISetClass* barUISetClass = nullptr;

protected:
	HRESULT RecreateDeviceResources(const Ui3RenderDeviceEpoch& epoch,
		UINT32 targetWidth, UINT32 targetHeight);
	void DiscardDeviceDependentCaches();

	struct FrameGradientBrushCacheClass
	{
		COLORREF color = RGB(0, 0, 0);
		BarBorderLightSourceEnum lightSource = BarBorderLightSourceEnum::Primary;
		ComPtr<ID2D1RadialGradientBrush> brush;
	};
	struct FrameDiffuseExactMaskKeyClass
	{
		UINT32 width = 0;
		UINT32 height = 0;
		FLOAT radiusX = 0.0F;
		FLOAT radiusY = 0.0F;
		bool Matches(const FrameDiffuseExactMaskKeyClass& other) const
		{
			return width == other.width && height == other.height
				&& radiusX == other.radiusX && radiusY == other.radiusY;
		}
	};
	struct FrameDiffuseExactMaskCandidateClass
	{
		FrameDiffuseExactMaskKeyClass key{};
		unsigned long long lastSeenFrame = 0;
		unsigned long long lastUse = 0;
		unsigned int consecutiveFrames = 0;
		bool valid = false;
	};
	struct FrameDiffuseExactMaskCacheClass
	{
		FrameDiffuseExactMaskKeyClass key{};
		unsigned long long lastUse = 0;
		std::uint64_t logicalBytes = 0;
		ComPtr<ID2D1Bitmap1> bitmap;
	};
	struct FrameDiffuseExactMaskSelectionClass
	{
		const FrameDiffuseExactMaskCacheClass* cache = nullptr;
		D2D1_RECT_F destination{};
	};
	struct FrameDiffuseMaskCacheClass
	{
		int radiusXQuarter = 0;
		int radiusYQuarter = 0;
		int strokeWidthQuarter = 0;
		int standardDeviationQuarter = 0;
		FLOAT padding = 0.0F;
		FLOAT radiusX = 0.0F;
		FLOAT radiusY = 0.0F;
		D2D1_SIZE_F size{};
		ComPtr<ID2D1Bitmap1> bitmap;
		// 精确整图只为连续稳定尺寸晋升；候选与成品均保持固定上限。
		array<FrameDiffuseExactMaskCandidateClass, 6> exactCandidates{};
		vector<FrameDiffuseExactMaskCacheClass> exactMasks;
	};
	struct FrameGeometryDiffuseMaskCacheClass
	{
		int widthQuarter = 0;
		int heightQuarter = 0;
		int geometryVariantQuarter = 0;
		int strokeWidthQuarter = 0;
		int standardDeviationQuarter = 0;
		FLOAT padding = 0.0F;
		D2D1_SIZE_F size{};
		ComPtr<ID2D1Bitmap1> bitmap;
	};
	struct SuperellipseGeometryCacheClass
	{
		FLOAT width = 0.0F;
		FLOAT height = 0.0F;
		FLOAT n = 0.0F;
		int segments = 0;
		FLOAT translatedX = 0.0F;
		FLOAT translatedY = 0.0F;
		ComPtr<ID2D1PathGeometry> localGeometry;
		ComPtr<ID2D1TransformedGeometry> translatedGeometry;
	};
	struct ThicknessFineDialLabelCacheClass
	{
		int value = 0;
		FLOAT zoom = 0.0F;
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

	// 返回强引用，避免缓存扩容或换 epoch 时使当前帧仍在使用的 brush 失效。
	ComPtr<ID2D1RadialGradientBrush> GetFrameGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, BarBorderLightSourceEnum lightSource);
	ID2D1SolidColorBrush* GetFrameSolidColorBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, double opacity);
	ID2D1LinearGradientBrush* GetThicknessPreviewGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT leftOpacity, FLOAT opacity);
	ID2D1LinearGradientBrush* GetColorPickerHueGradientBrush(
		ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint);
	ID2D1LinearGradientBrush* GetColorPickerToneGradientBrush(
		ID2D1DeviceContext* deviceContext, bool darkTone,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT opacity);
	void DrawProgressRing(ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
		FLOAT progress, COLORREF trackColor, COLORREF progressColor,
		FLOAT trackOpacity, FLOAT progressOpacity);
	ID2D1PathGeometry* GetThicknessPreviewPath();
	ID2D1StrokeStyle1* GetThicknessPreviewStrokeStyle();
	ID2D1StrokeStyle* GetRoundStrokeStyle();
	ID2D1PathGeometry* GetThicknessFineDialSelectorGeometry();
	ThicknessFineDialLabelCacheClass* GetThicknessFineDialLabelLayout(
		int value, FLOAT zoom);
	ID2D1Geometry* GetSuperellipseGeometry(
		FLOAT x, FLOAT y, FLOAT width, FLOAT height, FLOAT n, int segments);
	FrameDiffuseMaskCacheClass* GetRoundedRectDiffuseMask(
		ID2D1DeviceContext* deviceContext,
		const D2D1_ROUNDED_RECT& roundedRect, FLOAT strokeWidth);
	FrameDiffuseExactMaskSelectionClass ResolveRoundedRectExactMask(
		ID2D1DeviceContext* deviceContext,
		FrameDiffuseMaskCacheClass& mask,
		const D2D1_ROUNDED_RECT& roundedRect);
	HRESULT CreateRoundedRectExactMask(
		const FrameDiffuseMaskCacheClass& mask,
		const FrameDiffuseExactMaskKeyClass& key,
		ComPtr<ID2D1Bitmap1>& outputBitmap);
	unsigned int FillRoundedRectDiffuseMaskSlices(
		ID2D1DeviceContext* deviceContext, ID2D1Bitmap* bitmap,
		ID2D1Brush* brush, const FLOAT* sourceX, const FLOAT* sourceY,
		const FLOAT* destinationX, const FLOAT* destinationY,
		int segmentCount);
	void DrawRoundedRectDiffuseMask(ID2D1DeviceContext* deviceContext,
		const FrameDiffuseMaskCacheClass& mask,
		const FrameDiffuseExactMaskSelectionClass& exactMask,
		const D2D1_ROUNDED_RECT& roundedRect,
		ID2D1RadialGradientBrush* brush, FLOAT opacity);
	FrameGeometryDiffuseMaskCacheClass* GetGeometryDiffuseMask(
		ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
		FLOAT strokeWidth, int geometryVariantQuarter);
	void DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
		const FrameGeometryDiffuseMaskCacheClass& mask,
		const D2D1_RECT_F& geometryBounds,
		ID2D1RadialGradientBrush* brush, FLOAT opacity);
	bool DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
		BarUiFrameLightColorEnum frameLightColor,
		bool primaryLightEnabled, double cursorLightIntensityScale,
		double baseFramePct, double lightPct, FLOAT strokeWidth,
		const D2D1_ROUNDED_RECT* roundedRect,
		ID2D1Geometry* geometry, int geometryVariantQuarter = 0);

	ComPtr<ID2D1DeviceContext> deviceContext;
	ComPtr<ID2D1Bitmap1> targetBitmap;
	ComPtr<ID2D1GdiInteropRenderTarget> gdiInteropRenderTarget;
	unsigned long long deviceGeneration = 0;
	UINT32 targetBitmapWidth = 0;
	UINT32 targetBitmapHeight = 0;

	D2D1_POINT_2F framePrimaryLight = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightStart = D2D1::Point2F();
	D2D1_POINT_2F framePrimaryLightTarget = D2D1::Point2F();
	D2D1_POINT_2F frameCursorLight = D2D1::Point2F();
	D2D1_POINT_2F frameCursorScreenLight = D2D1::Point2F();
	D2D1_POINT_2F frameLocalCursorLight = D2D1::Point2F();
	FLOAT frameCursorLightIntensity = 0.0F;
	FLOAT frameCursorLightIntensityStart = 0.0F;
	FLOAT frameCursorLightIntensityTarget = 0.0F;
	FLOAT frameLightRadius = 0.0F;
	FLOAT frameCursorLightRadius = 0.0F;
	FLOAT frameLocalCursorLightRadiusX = 0.0F;
	FLOAT frameLocalCursorLightRadiusY = 0.0F;
	BarBorderPrimaryAnchorEnum framePrimaryLightAnchor = BarBorderPrimaryAnchorEnum::MainButton;
	bool framePrimaryLightAnchorInitialized = false;
	bool framePrimaryLightAnimating = false;
	bool frameCursorLightVisible = false;
	bool frameCursorLightAnimating = false;
	bool framePrimaryLightChanged = false;
	bool frameCursorLightChanged = false;
	bool framePrimaryLightWasAnimating = false;
	bool frameCursorLightWasAnimating = false;
	bool frameAnimationStateInitialized = false;
	bool frameLastAnimationEnabled = false;
	bool frameCursorInputAvailable = false;
	bool frameLightingWasAnimating = false;
	bool frameEdgeLightingEnabled = false;
	bool frameGradientFailureLogged = false;
	bool frameGradientUnavailable = false;
	bool thicknessPreviewGradientFailureLogged = false;
	bool thicknessPreviewGradientUnavailable = false;
	bool thicknessPreviewPathFailureLogged = false;
	bool thicknessPreviewPathUnavailable = false;
	bool roundStrokeStyleFailureLogged = false;
	bool roundStrokeStyleUnavailable = false;
	bool thicknessFineDialSelectorUnavailable = false;
	bool thicknessFineDialSelectorFailureLogged = false;
	bool colorPickerGradientFailureLogged = false;
	bool colorPickerGradientUnavailable = false;
	bool frameDiffuseEffectFailureLogged = false;
	bool frameDiffuseMaskFailureLogged = false;
	bool frameDiffuseMaskUnavailable = false;
	bool frameDiffuseMaskCreatedThisFrame = false;
	bool frameDiffuseExactMaskUnavailable = false;
	double frameDiffuseMaskGeometryScale = 1.0;
	unsigned long long frameDiffuseMaskFrameSerial = 0;
	unsigned long long frameDiffuseExactMaskUseSerial = 0;
	unsigned long long frameDiffuseExactMaskPromotionFrameSerial = 0;
	double frameZoom = 1.0;
	bool frameDirtyClipActive = false;
	D2D1_RECT_F frameDirtyClipRect{};
	double framePrimaryLightMoveElapsed = 0.0;
	double frameCursorLightFadeElapsed = 0.0;
	double frameDrawingPenColorElapsed = 0.0;
	double frameDrawingModeTransitionElapsed = 0.0;
	double frameDrawingPenColorBlend = 0.0;
	double frameDrawingPenColorBlendStart = 0.0;
	double frameDrawingPenColorBlendTarget = 0.0;
	double frameDrawingLightOpacity = 1.0;
	double frameDrawingLightOpacityStart = 1.0;
	unsigned long long handledBorderCursorLightSerial = 0;
	COLORREF frameDrawingPenColor = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorStart = RGB(0, 0, 0);
	COLORREF frameDrawingPenColorTarget = RGB(0, 0, 0);
	bool frameDrawingUsesPenColor = false;
	bool frameDrawingPenColorInitialized = false;
	bool frameDrawingPenColorAnimating = false;
	bool frameDrawingModeInitialized = false;
	bool frameDrawingModeTransitionAnimating = false;
	bool frameDrawingPenColorSourceInitialized = false;
	bool frameDrawingPenColorCarriesHighlighterHistory = false;
	int frameDrawingMode = -1;
	int frameDrawingPenColorSource = -1;
	vector<FrameGradientBrushCacheClass> frameGradientBrushCache;
	vector<FrameDiffuseMaskCacheClass> frameDiffuseMaskCache;
	vector<FrameGeometryDiffuseMaskCacheClass> frameGeometryDiffuseMaskCache;
	ComPtr<ID2D1SolidColorBrush> frameSolidColorBrush;
	array<ThicknessPreviewGradientBrushCacheClass, 64>
		thicknessPreviewGradientBrushCache{};
	std::uint64_t thicknessPreviewGradientUseSerial = 0;
	ComPtr<ID2D1LinearGradientBrush> colorPickerHueGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerLightGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerDarkGradientBrush;
	ComPtr<ID2D1PathGeometry> thicknessPreviewPath;
	ComPtr<ID2D1StrokeStyle1> thicknessPreviewStrokeStyle;
	ComPtr<ID2D1StrokeStyle> roundStrokeStyle;
	ComPtr<ID2D1PathGeometry> thicknessFineDialSelectorGeometry;
	array<ThicknessFineDialLabelCacheClass, 64>
		thicknessFineDialLabelCache{};
	unsigned long long thicknessFineDialLabelUseSerial = 0;
	SuperellipseGeometryCacheClass superellipseGeometryCache;
	ComPtr<ID2D1DeviceContext> frameMaskDeviceContext;
	ComPtr<ID2D1Effect> frameGaussianBlurEffect;
	ComPtr<ID2D1SolidColorBrush> frameDiffuseExactMaskBrush;

	friend class BarUISetClass;
	friend class BarRenderLoopCoordinator;
};
