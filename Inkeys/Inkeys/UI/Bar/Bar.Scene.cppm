module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d2d1_1.h>
#include <dwrite_1.h>
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module Inkeys.UI.Bar:Scene;

export import Inkeys.UI.Bar.Metrics;
export import Inkeys.UI.Bar.SurfaceLayout;
import Inkeys.UI.RenderPipeline;

export namespace Inkeys::UI::Bar
{
	struct BarSurfaceBackgroundSpec
	{
		BarSurfaceDipRect bounds{};
		COLORREF fill = RGB(0, 0, 0);
		COLORREF frame = RGB(0, 0, 0);
		bool useThemeColors = true;
		double cornerRadiusDip = 0.0;
		double frameThicknessDip = 0.0;
		double fillOpacity = 1.0;
		double frameOpacity = 0.0;
		bool visible = true;
	};

	using BarSurfaceWidgetId = std::uint32_t;
	inline constexpr BarSurfaceWidgetId BarSurfaceNoWidget = 0;

	enum class BarSurfaceWidgetKind : std::uint8_t
	{
		Button,
		DragHandle,
	};

	struct BarSurfaceWidgetSpec
	{
		BarSurfaceWidgetId id = BarSurfaceNoWidget;
		BarSurfaceWidgetKind kind = BarSurfaceWidgetKind::Button;
		BarButtonVisualLayoutKind layoutKind =
			BarButtonVisualLayoutKind::Custom;
		BarSurfaceDipRect bounds{};
		bool visible = true;
		bool enabled = true;
		// 临时输入锁不改变控件的 enabled 外观和内容透明度。
		bool interactive = true;
		bool selected = false;
		std::wstring iconResource;
		std::wstring primaryText;
		std::wstring secondaryText;
		std::optional<double> iconAngle;
		double iconSizeDip = BarButtonTwoTwoIconSizeDip;
		double iconOffsetXDip = 0.0;
		double iconOffsetYDip = BarButtonTwoTwoIconOffsetYDip;
		double primaryFontSizeDip = BarButtonTwoTwoIconSizeDip;
		double primaryOffsetXDip = 0.0;
		double primaryOffsetYDip = BarButtonTwoTwoIconOffsetYDip;
		double primarySlotWidthDip = 0.0;
		double primarySlotHeightDip = BarButtonTwoTwoIconSizeDip;
		double secondaryFontSizeDip = BarButtonTwoTwoLabelFontSizeDip;
		double secondaryOffsetXDip = 0.0;
		double secondaryOffsetYDip = BarButtonTwoTwoLabelOffsetYDip;
		double secondarySlotWidthDip = 0.0;
		double secondarySlotHeightDip = BarButtonTwoTwoLabelHeightDip;
		double dragOpacity = 0.72;
		COLORREF fill = RGB(0, 0, 0);
		COLORREF content = RGB(0, 0, 0);
		bool useThemeColors = true;
		std::function<void()> onClick;
	};

	struct BarSurfaceWidgetLayout
	{
		BarSurfaceWidgetId id = BarSurfaceNoWidget;
		BarSurfaceWidgetKind kind = BarSurfaceWidgetKind::Button;
		RECT localPixels{};
		bool visible = false;
		bool enabled = false;
		bool interactive = false;
		bool selected = false;
	};

	struct BarSurfaceLayout
	{
		RECT logicalBounds{};
		RECT presentationBounds{};
		// 兼容旧调用方；该字段与 logicalBounds 保持相同的全局像素矩形。
		RECT surfacePixels{};
		float dpiScale = 1.0F;
		std::vector<BarSurfaceWidgetLayout> widgets;
	};

	enum class BarSurfacePointerEventKind : std::uint8_t
	{
		Move,
		Leave,
		Down,
		Up,
		Cancel,
	};

	struct BarSurfacePointerResult
	{
		BarSurfaceWidgetId hover = BarSurfaceNoWidget;
		BarSurfaceWidgetId pressed = BarSurfaceNoWidget;
		BarSurfaceWidgetId clicked = BarSurfaceNoWidget;
		bool hoverChanged = false;
		bool pressedChanged = false;
		bool consumed = false;
	};

	struct BarSurfaceRenderResult
	{
		bool rendered = false;
		bool animationActive = false;
		bool invalidated = false;
		RECT damage{};
	};

	struct BarSurfaceHooks
	{
		// invalidate 只标记该 surface；wake 负责唤醒独立的 RenderPipeline client。
		std::function<void()> invalidate;
		std::function<void()> wake;
	};

	// MainBar 以全局像素发布第一光源；各 Scene 在自己的 presentation target 内映射。
	struct BarSurfaceSharedPrimaryLight
	{
		double screenX = 0.0;
		double screenY = 0.0;
		double radiusPixels = 0.0;
		COLORREF drawingPenColor = RGB(0, 0, 0);
		double drawingPenColorBlend = 0.0;
		double drawingLightOpacity = 1.0;
		bool visible = false;
		bool edgeLightingEnabled = false;
	};

	class BarSurfaceScene
	{
	public:
		BarSurfaceScene();
		~BarSurfaceScene();
		BarSurfaceScene(const BarSurfaceScene&) = delete;
		BarSurfaceScene& operator=(const BarSurfaceScene&) = delete;
		BarSurfaceScene(BarSurfaceScene&&) noexcept;
		BarSurfaceScene& operator=(BarSurfaceScene&&) noexcept;

		// 配置背景和按钮拓扑；同一 id 的按钮会保留 hover/pressed 状态。
		bool Configure(const BarSurfaceBackgroundSpec& background,
			std::span<const BarSurfaceWidgetSpec> widgets);
		// 按标准 twoTwo 网格横排三个按钮；调用方只需提供按钮内容和回调。
		bool ConfigureHorizontalTwoTwoGroup(
			const BarSurfaceBackgroundSpec& background,
			std::span<const BarSurfaceWidgetSpec> widgets);
		// 按屏幕边缘定位的通用横向组；自动写入 logical bounds 和各按钮局部 DIP 布局。
		bool ConfigureHorizontalGroup(
			const BarSurfaceBackgroundSpec& background,
			std::span<const BarSurfaceWidgetSpec> widgets,
			const BarSurfaceHorizontalGroupSpec& group);
		bool SetWidgets(std::span<const BarSurfaceWidgetSpec> widgets);
		// 复用稳定 id，从当前呈现几何连续过渡到新背景和按钮布局。
		bool TransitionLayout(const BarSurfaceBackgroundSpec& background,
			std::span<const BarSurfaceWidgetSpec> widgets,
			double durationSeconds = 0.2);
		bool SetWidgetState(BarSurfaceWidgetId id, bool visible, bool enabled,
			std::wstring primaryText = {}, std::wstring secondaryText = {},
			std::optional<std::wstring> iconResource = std::nullopt,
			std::optional<double> iconAngle = std::nullopt);
		bool SetWidgetInteractive(BarSurfaceWidgetId id, bool interactive);
		bool SetWidgetSelected(BarSurfaceWidgetId id, bool selected);
		bool SetWidgetExternalPressed(BarSurfaceWidgetId id, bool pressed);
		bool SetBounds(RECT logicalBounds, float dpiScale) noexcept;
		void SetBackground(const BarSurfaceBackgroundSpec& background);
		void SetOpacity(double opacity,
			double durationSeconds = 0.2) noexcept;
		void SetDamageOutsetDip(double outsetDip) noexcept;
		void SetHooks(BarSurfaceHooks hooks);
		// 订阅者只在共享第一光源的最终像素变化时被唤醒。
		void SetSharedPrimaryLightSubscribed(bool subscribed) noexcept;
		static void PublishSharedPrimaryLight(
			const BarSurfaceSharedPrimaryLight& light) noexcept;

		void Reset() noexcept;
		void ReleaseDeviceResources() noexcept;
		void Invalidate() noexcept;
		[[nodiscard]] bool IsInvalidated() const noexcept;
		[[nodiscard]] RECT PendingDamage() const noexcept;
		[[nodiscard]] RECT ConsumeDamage() noexcept;
		[[nodiscard]] BarSurfaceLayout Layout() const;
		[[nodiscard]] RECT LogicalBounds() const noexcept;
		[[nodiscard]] RECT PresentationBounds() const noexcept;
		[[nodiscard]] LONG PresentationOutsetPixels() const noexcept;
		[[nodiscard]] HRESULT EnsureDeviceResources(
			const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch,
			UINT width, UINT height);
		[[nodiscard]] ID2D1DeviceContext* DeviceContext() const noexcept;
		[[nodiscard]] ID2D1GdiInteropRenderTarget* GdiInteropRenderTarget() const noexcept;
		void HandleFrameEndDrawResult(HRESULT result);
		// 由 HWND 局部坐标转为逻辑内容坐标；透明 presentation margin 返回 nullopt。
		[[nodiscard]] std::optional<POINT> PresentationToLogical(
			POINT presentationLocalPixels) const noexcept;
		[[nodiscard]] BarSurfaceWidgetId HitTest(POINT localPixels) const noexcept;
		[[nodiscard]] BarSurfaceWidgetId HitTestPresentation(
			POINT presentationLocalPixels) const noexcept;

		BarSurfacePointerResult PointerMove(POINT localPixels) noexcept;
		BarSurfacePointerResult PointerLeave() noexcept;
		BarSurfacePointerResult PointerDown(POINT localPixels) noexcept;
		BarSurfacePointerResult PointerUp(
			POINT localPixels, bool invokeCallback = true) noexcept;
		BarSurfacePointerResult CancelPointer() noexcept;

		// 渲染线程调用；该函数只提交本 surface 的 Shape/Button，不改变其他 Bar client。
		BarSurfaceRenderResult Render(ID2D1DeviceContext* deviceContext,
			std::chrono::steady_clock::time_point frameTime);
		[[nodiscard]] bool Advance(std::chrono::steady_clock::time_point frameTime) noexcept;
		[[nodiscard]] bool AnimationActive() const noexcept;

	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
