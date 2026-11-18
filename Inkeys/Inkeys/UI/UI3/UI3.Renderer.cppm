module;

#include <windows.h>

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <wrl/client.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <tuple>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <algorithm>
#include <cmath>

export module Inkeys.UI.UI3:Renderer;

import :Primitive;
import :Format;
import :Envelope;
import :Theme;
import :Lighting;

import Inkeys.UI.UI3.Animation;
import Inkeys.UI.UI3.Metrics;
import Inkeys.UI.RenderPipeline;
export import Inkeys.UI.UI3.Material;

using namespace std;
using Microsoft::WRL::ComPtr;

namespace Inkeys::UI::UI3
{
using Ui3RenderDeviceEpoch = Inkeys::UI::RenderPipeline::DeviceEpoch;

constexpr double SvgRasterUpscaleThreshold = 1.35;
constexpr double SvgRasterSizeEpsilon = 0.01;
constexpr double BorderFrameDiffuseOpacity = ThemeMaterial::DarkFrameDiffuseOpacity;
constexpr double BorderPenDiffuseOpacity = ThemeMaterial::DarkPenDiffuseOpacity;
// 标准差等于线宽时，1px 线源经过一维 Gaussian 后中心约保留 38.3%。
constexpr double BorderGaussianCenterCoverage = 0.382924922548;

enum class BorderLightSourceEnum : int
{
	Primary,
	Cursor,
};
export struct LocalizedTypography
{
	std::uint64_t revision = 0;
	wstring fontFamily = L"HarmonyOS Sans SC";
	wstring textLocale = L"zh-cn";
};
export struct UIRenderingServices
{
	std::function<FormatCache*()> typography;
	std::function<void()> resetDeviceCaches;
	// 版本查询保持轻量；只有配置或版本变化时才读取字族与 locale。
	std::function<std::uint64_t()> localizedTypographyRevision;
	std::function<LocalizedTypography()> localizedTypography;
};

// 具体渲染
export class PanelPoseScope;
export class UIRendering
{
	friend class PanelPoseScope; // 同模块面板作用域只保存/恢复局部绘制姿态。
public:
	UIRendering() = default;
	explicit UIRendering(UIRenderingServices services) : services_(std::move(services)) {}

public:
	ID2D1SolidColorBrush* ContentSolidBrush(COLORREF color, double opacity = 1.0)
	{ return GetFrameSolidColorBrush(deviceContext.Get(), color, opacity); }
	// 画笔与当前图形共用色板渐变资源和色义，调用方仅适配目标。
	void ColorPalette(ID2D1DeviceContext*, D2D1_RECT_F, bool darkTone, bool below, double opacity = 1);
	bool Shape(ID2D1DeviceContext* deviceContext, const UiShapeClass& shape, const UiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Superellipse(ID2D1DeviceContext* deviceContext, const UiSuperellipseClass& superellipse, const UiInheritClass& inh, RECT* targetRect = nullptr, bool clip = false);
	bool Svg(ID2D1DeviceContext* deviceContext, UiSVGClass& svg, const UiInheritClass& inh);
	bool Png(ID2D1DeviceContext* deviceContext, UiPNGClass& png, const UiInheritClass& inh);
	bool Word(ID2D1DeviceContext* deviceContext, const UiWordClass& word, const UiInheritClass& inh, DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_BOLD, DWRITE_TEXT_ALIGNMENT textAlign = DWRITE_TEXT_ALIGNMENT_CENTER, bool fitButtonLabel = false);
	D2D1_SIZE_F MeasureText(const wstring& content, double fontSize,
		DWRITE_FONT_WEIGHT fontWeight = DWRITE_FONT_WEIGHT_NORMAL);
	double FitButtonLabelFontSize(const wstring& content, double fontSize, double width, double height,
		DWRITE_FONT_WEIGHT weight = DWRITE_FONT_WEIGHT_BOLD);
	void ConfigureLocalizedTypography();
	bool RefreshLocalizedTypography();
	bool UpdateFontRenderingPolicy(bool allowFractional) noexcept;
	[[nodiscard]] FLOAT ResolveFontSize(ID2D1DeviceContext* context, double fontSize, double nominalTarget = -1.0);
	[[nodiscard]] FLOAT ResolveWordFontSize(ID2D1DeviceContext* context, const UiWordClass& word,
		double fontSize, double nominalTarget = -1.0);
	void BeginFontFrame(std::chrono::steady_clock::time_point time,
		const UiAnimationAdvanceContextClass& context, double targetZoom);
	void SetFontTargetZoom(double zoom) noexcept { fontTargetZoom = zoom; }
	void SetFontTargetScale(double scale) noexcept { fontTargetY = {0.0,scale}; fontTargetTransformReady = true; }
	[[nodiscard]] bool FontAnimationsActive() const noexcept { return fontAnimationsActive; }
	class FontTargetScope
	{
	public:
		FontTargetScope(UIRendering& owner, double targetScale, double duration = 0.0) noexcept
			: owner_(owner), targetY_(owner.fontTargetY), ready_(owner.fontTargetTransformReady), duration_(owner.fontDuration)
		{
			if (!owner_.fontTargetTransformReady && owner_.deviceContext)
			{
				D2D1_MATRIX_3X2_F matrix{}; owner_.deviceContext->GetTransform(&matrix);
				owner_.fontTargetY = {matrix._21,matrix._22};
			}
			// 显式组合目标向量，避免用 FLOAT 当前矩阵除 double 当前倍率使整数目标失真。
			if (std::isfinite(targetScale) && targetScale >= 0.0)
				for (double& axis : owner_.fontTargetY) axis *= targetScale;
			owner_.fontTargetTransformReady = true;
			if (duration > 0.0) owner_.fontDuration = duration;
		}
		~FontTargetScope() { owner_.fontTargetY = targetY_; owner_.fontTargetTransformReady = ready_; owner_.fontDuration = duration_; }
	private:
		UIRendering& owner_; std::array<double,2> targetY_; bool ready_; double duration_;
	};
	[[nodiscard]] const wstring& GetFontFamily() const noexcept
	{
		return fontFamily;
	}
	[[nodiscard]] const wstring& GetTextLocale() const noexcept
	{
		return textLocale;
	}
	[[nodiscard]] RECT GetFramePrimaryLightDamageBounds() const noexcept;
	[[nodiscard]] RECT GetFrameCursorLightDamageBounds() const noexcept;
	[[nodiscard]] UiFrameLightingSnapshot
		SnapshotFrameLighting() const noexcept;
	void SetFrameLightingSnapshot(
		const UiFrameLightingSnapshot& snapshot, bool edgeLightingEnabled) noexcept;
	// 独立表面可在复用旧快照绘制前刷新总开关，不改变快照自身的状态机结果。
	void SetFrameEdgeLightingAllowed(bool enabled) noexcept
	{ frameEdgeLightingAllowed = enabled; }
	void SetFrameZoom(double zoom)
	{
		frameZoom = std::isfinite(zoom) && zoom > 0.0 ? zoom : 1.0;
	}
	[[nodiscard]] double GetFrameZoom() const noexcept
	{
		return frameZoom;
	}
	// 常驻内容（主栏/主按钮/分页的图标与文字）透明度倍率；调用方在绘制完常驻内容后恢复为 1。
	// 仅常驻对象绘制段启用，退出该段必须恢复，避免污染属性/更多等面板。
	void SetResidentIdleFade(double progress) noexcept
	{ residentIdleFade = ThemeMaterial::ClampWeight(progress); }
	void SetResidentContentOpacity(double opacity) noexcept
	{
		residentContentOpacity = std::isfinite(opacity) ? std::clamp(opacity, 0.0, 1.0) : 1.0;
	}
	// 目标资源与帧生命周期归帧目标（:Host）所有；绘制器只记录所绑定帧目标的设备上下文
	// 与自身缓存所属的设备代次，二者由下面的通告入口更新。
	[[nodiscard]] ID2D1DeviceContext* GetDeviceContext() const noexcept
	{
		return deviceContext.Get();
	}
	[[nodiscard]] unsigned long long GetDeviceGeneration() const noexcept
	{
		return deviceGeneration;
	}
	// —— 帧目标通告入口（仅供 :Host 的帧目标调用）——
	// 目标资源创建/重建成功，或绑定到已有资源的帧目标。
	void NotifyFrameTargetReady(ID2D1DeviceContext* context, unsigned long long generation) noexcept;
	// 目标资源丢弃或即将被替换：清空设备相关缓存并触发 resetDeviceCaches 服务。
	void NotifyFrameTargetDiscarded();
	// 帧目标推入/弹出脏区裁剪：推进遮罩帧序号，并记录裁剪供 ShouldDrawFrameBounds 判定。
	void NotifyFrameDirtyClipPushed(ID2D1DeviceContext* context, const D2D1_RECT_F& dirtyRect);
	void NotifyFrameDirtyClipPopped() noexcept;
	// 外部上下文且不由帧目标裁剪的帧：只推进帧序号，不启用内容剔除。
	void NotifyFrameBegunWithoutClip();
	// EndDraw 之后：处理遮罩失败并清理自有文字格式缓存。
	void NotifyFrameEnded(HRESULT endDrawResult);
	// bounds 使用当前 DC 的坐标；按当前变换与推入时的真实 clip 比较，未知时保留原绘制。
	[[nodiscard]] bool ShouldDrawFrameBounds(ID2D1DeviceContext* context,
		const D2D1_RECT_F& bounds, double outset = 0.0) noexcept;
	[[nodiscard]] double GetFrameShapeDrawOutset(const UiShapeClass& shape) const noexcept;
	[[nodiscard]] unsigned long long GetFrameCulledDrawCount() const noexcept
	{ return frameCulledDrawCount; }
	void HandleFrameEndDrawResult(HRESULT endDrawResult);
	void SetFrameDiffuseReferenceZoom(double zoom) noexcept
	{ frameDiffuseReferenceZoom = std::isfinite(zoom) && zoom > 0.0 ? zoom : 0.0; }
	void PushPanelOpacity(ID2D1DeviceContext* context, const D2D1_RECT_F& bounds, double opacity);
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

	// 供页面表面绘制旋转圆环；沿用受保护的 DrawProgressRing 实现（同色轨道与进度，仅不透明度不同）。
	void DrawRing(ID2D1DeviceContext* deviceContext, D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
		FLOAT progress, COLORREF color, FLOAT trackOpacity, FLOAT progressOpacity)
	{
		DrawProgressRing(deviceContext, center, radius, strokeWidth, progress, color, color, trackOpacity, progressOpacity);
	}
public:
	[[nodiscard]] bool IsFontFrameReady() const noexcept { return fontFrameReady; }
	[[nodiscard]] double GetResidentIdleFade() const noexcept { return residentIdleFade; }
	[[nodiscard]] ComPtr<ID2D1Factory1> GetGeometryFactory() const
	{ return Inkeys::UI::RenderPipeline::D2DFactory(); }
	FormatCache* GetTypographyCache();
protected:
	UIRenderingServices services_;
	std::unique_ptr<FormatCache> ownedTypography_;
	void DiscardDeviceDependentCaches();
	void AdvanceFrameDiffuseMaskSerial();

	struct FrameGradientBrushCacheClass
	{
		COLORREF color = RGB(0, 0, 0);
		BorderLightSourceEnum lightSource = BorderLightSourceEnum::Primary;
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

	// 返回强引用，避免缓存扩容或换 epoch 时使当前帧仍在使用的 brush 失效。
	ComPtr<ID2D1RadialGradientBrush> GetFrameGradientBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color,
		BorderLightSourceEnum lightSource, FLOAT radiusScale = 1.0F);
public:
	ID2D1SolidColorBrush* GetFrameSolidColorBrush(
		ID2D1DeviceContext* deviceContext, COLORREF color, double opacity);
	// 编辑器复用主栏既有色板渐变与双颜色保持圆环。
	ID2D1LinearGradientBrush* GetColorPickerHueGradientBrush(
		ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint);
	ID2D1LinearGradientBrush* GetColorPickerToneGradientBrush(
		ID2D1DeviceContext* deviceContext, bool darkTone,
		D2D1_POINT_2F startPoint, D2D1_POINT_2F endPoint,
		FLOAT opacity);
	// 构造提示复用双配色进度环；圆头可选，原有调用默认保持原样。
	bool DrawProgressRing(ID2D1DeviceContext* deviceContext,
		D2D1_POINT_2F center, FLOAT radius, FLOAT strokeWidth,
		FLOAT progress, COLORREF trackColor, COLORREF progressColor,
		FLOAT trackOpacity, FLOAT progressOpacity, bool roundCaps = false);
	ID2D1StrokeStyle* GetRoundStrokeStyle();
protected:
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
public:
	FrameGeometryDiffuseMaskCacheClass* GetGeometryDiffuseMask(
		ID2D1DeviceContext* deviceContext, ID2D1Geometry* geometry,
		FLOAT strokeWidth, int geometryVariantQuarter);
protected:
	void DrawGeometryDiffuseMask(ID2D1DeviceContext* deviceContext,
		const FrameGeometryDiffuseMaskCacheClass& mask,
		const D2D1_RECT_F& geometryBounds,
		ID2D1RadialGradientBrush* brush, FLOAT opacity);
public:
	bool DrawPointLightFrame(ID2D1DeviceContext* deviceContext, COLORREF color,
		const ThemeMaterial::Material& material,
		UiFrameLightColorEnum frameLightColor,
		bool primaryLightEnabled, double cursorLightIntensityScale,
		double lightCursorLightIntensityScale,
		double baseFramePct, double lightPct, FLOAT strokeWidth,
		const D2D1_ROUNDED_RECT* roundedRect,
		ID2D1Geometry* geometry, int geometryVariantQuarter = 0);
protected:
	void DrawSurfaceMaterialShadows(ID2D1DeviceContext* deviceContext,
		const ThemeMaterial::Material& material, double opacity,
		const D2D1_ROUNDED_RECT* roundedRect, ID2D1Geometry* geometry);
	void DrawSurfaceMaterialHighlight(ID2D1DeviceContext* deviceContext,
		const ThemeMaterial::Material& material, double opacity,
		const D2D1_ROUNDED_RECT* roundedRect, ID2D1Geometry* geometry);

	// 所绑定帧目标的设备上下文（共享引用，资源归帧目标）与缓存所属设备代次。
	ComPtr<ID2D1DeviceContext> deviceContext;
	unsigned long long deviceGeneration = 0;

	D2D1_POINT_2F framePrimaryLight = D2D1::Point2F();
	D2D1_POINT_2F frameCursorLight = D2D1::Point2F();
	D2D1_POINT_2F frameCursorScreenLight = D2D1::Point2F();
	D2D1_POINT_2F frameLocalCursorLight = D2D1::Point2F();
	FLOAT frameCursorLightIntensity = 0.0F;
	FLOAT frameLightRadius = 0.0F;
	FLOAT frameCursorLightRadius = 0.0F;
	FLOAT frameLocalCursorLightRadiusX = 0.0F;
	FLOAT frameLocalCursorLightRadiusY = 0.0F;
	bool framePrimaryLightAnchorInitialized = false;
	bool frameCursorLightVisible = false;
	bool frameEdgeLightingEnabled = false;
	bool frameEdgeLightingAllowed = true;
	bool frameGradientFailureLogged = false;
	bool frameGradientUnavailable = false;

	bool roundStrokeStyleFailureLogged = false;
	bool roundStrokeStyleUnavailable = false;

	bool colorPickerGradientFailureLogged = false;
	bool colorPickerGradientUnavailable = false;
	bool frameDiffuseEffectFailureLogged = false;
	bool frameDiffuseMaskFailureLogged = false;
	bool frameDiffuseMaskUnavailable = false;
	bool frameDiffuseMaskCreatedThisFrame = false;
	bool frameDiffuseExactMaskUnavailable = false;
	double frameDiffuseMaskGeometryScale = 1.0;
	double frameDiffuseReferenceZoom = 0.0;
	unsigned long long frameDiffuseMaskFrameSerial = 0;
	unsigned long long frameDiffuseExactMaskUseSerial = 0;
	unsigned long long frameDiffuseExactMaskPromotionFrameSerial = 0;
	double frameZoom = 1.0;
	double residentContentOpacity = 1.0;
	double residentIdleFade = 0.0;
	bool frameDirtyClipActive = false;
	D2D1_RECT_F frameDirtyClipDeviceRect{};
	ID2D1DeviceContext* frameDirtyClipContext = nullptr;
	bool frameDirtyClipDeviceValid = false;
	unsigned long long frameCulledDrawCount = 0;
	double frameDrawingPenColorBlend = 0.0;
	double frameDrawingLightOpacity = 1.0;
	COLORREF frameDrawingPenColor = RGB(0, 0, 0);
	vector<FrameGradientBrushCacheClass> frameGradientBrushCache;
	vector<FrameDiffuseMaskCacheClass> frameDiffuseMaskCache;
	vector<FrameGeometryDiffuseMaskCacheClass> frameGeometryDiffuseMaskCache;
	ComPtr<ID2D1SolidColorBrush> frameSolidColorBrush;
	ComPtr<ID2D1LinearGradientBrush> surfaceHighlightBrush;
	bool surfaceHighlightUnavailable = false;

	ComPtr<ID2D1LinearGradientBrush> colorPickerHueGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerLightGradientBrush;
	ComPtr<ID2D1LinearGradientBrush> colorPickerDarkGradientBrush;

	ComPtr<ID2D1StrokeStyle> roundStrokeStyle;

public:
	FLOAT ResolveAnimatedFontSize(ID2D1DeviceContext*, double currentSize, double nominalTarget,
		UiFontCorrectionClass&, double duration = 0.0);
protected:
	bool allowFractionalFontSize = false;
	bool fontFrameReady = false, fontAnimationsActive = false;
	std::uint64_t fontFrameSerial = 0;
	double fontTargetZoom = 0.0, fontDuration = 0.0;
	std::array<double,2> fontTargetY{0.0,1.0};
	bool fontTargetTransformReady = false;
	UiAnimationAdvanceContextClass fontFrameContext;
	UiFontCorrectionClass directFontCorrection;
	std::uint64_t localeRevision = 0;
	wstring fontFamily = L"HarmonyOS Sans SC";
	wstring textLocale = L"zh-cn";
	using ButtonLabelFitKey = std::tuple<wstring,double,double,double,unsigned>;
	std::map<ButtonLabelFitKey,double> buttonLabelFitCache;
	SuperellipseGeometryCacheClass superellipseGeometryCache;
	ComPtr<ID2D1DeviceContext> frameMaskDeviceContext;
	ComPtr<ID2D1Effect> frameGaussianBlurEffect;
	ComPtr<ID2D1SolidColorBrush> frameDiffuseExactMaskBrush;

};

// 输入几何均为像素；主栏保留每层独立合成，编辑器可对整枚Thumb统一淡出。
export struct SliderThumbStyle
{
	D2D1_POINT_2F centerPx{};
	FLOAT outerDiameterPx = 20, centerDiameterPx = 12;
	double accentOpacity = 1, lightWeight = 0, opacity = 1;
	FLOAT frameThicknessPx = 1;
	COLORREF surfaceColor = RGB(0, 0, 0), accentColor = RGB(88, 255, 236);
	bool compositeOpacity = false;
};
export void DrawSliderThumb(UIRendering& renderer, ID2D1DeviceContext* dc,
	const SliderThumbStyle& style, UiSVGClass* iconSvg = nullptr, double iconScale = 1.0);

}
