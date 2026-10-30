module;

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d2d1_1.h>
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module Inkeys.UI.Bar:Scene;
export import Inkeys.UI.Bar.Metrics;
export import Inkeys.UI.Bar.SurfaceLayout;
export class BarUIRendering;
import Inkeys.UI.RenderPipeline;

export namespace Inkeys::UI::Bar
{
	struct BarSurfaceBackgroundSpec
	{
		BarSurfaceDipRect bounds{};
		COLORREF fill = RGB(0, 0, 0), frame = RGB(0, 0, 0);
		bool useThemeColors = true;
		double cornerRadiusDip = 0, frameThicknessDip = 0;
		double fillOpacity = 1, frameOpacity = 0;
		bool visible = true;
		std::function<void(ID2D1DeviceContext*, D2D1_RECT_F)> drawContent;
	};

	using BarSurfaceButtonId = std::uint32_t;
	inline constexpr BarSurfaceButtonId BarSurfaceNoButton = 0;
	enum class BarPageContentKind : std::uint8_t { Numeric, Status };
	struct BarPageSurfaceContent
	{
		BarPageContentKind kind = BarPageContentKind::Numeric;
		unsigned surface = 0;
		bool whiteboard = false, switching = false, pptActive = false;
		bool nextIsAdd = false, nextIsEndShow = false, unsaved = false;
		int menu = 0;
		std::array<bool, 3> enabled{ true, true, true };
		std::array<bool, 3> interactive{ true, true, true };
		std::wstring currentPage, totalPage;
		std::wstring previousLabel, nextLabel, moreLabel, unsavedLabel;
		std::array<std::wstring, 3> actionLabels;
		std::array<std::wstring, 3> patternLabels;
		std::array<std::wstring, 2> sectionLabels;
		std::uint32_t backgroundRgb = 0x123B32;
		int pattern = 0;
	};

	struct BarSurfaceButtonLayout
	{
		BarSurfaceButtonId id = 0;
		RECT localPixels{};
		bool visible = false, enabled = false, interactive = false;
		const void* identity = nullptr;
		unsigned drawOrder = 0;
		double contentOpacity = 0;
	};
	struct BarPageSurfaceLayout
	{
		RECT logicalBounds{}, presentationBounds{};
		float dpiScale = 1;
		std::vector<BarSurfaceButtonLayout> buttons;
	};
	struct BarPageSurfaceGeometry
	{
		BarSurfaceDipRect bodyBounds{};
		double opacity = 1, fillOpacity = 1;
		std::uint64_t layoutRevision = 0;
		bool layoutAnimating = false;
	};
	struct BarSurfacePointerResult
	{
		BarSurfaceButtonId hover = 0, pressed = 0, clicked = 0;
		bool hoverChanged = false, pressedChanged = false, consumed = false;
	};
	enum class BarSurfacePointerEventKind { Move, Down, Up, Leave, Cancel };
	struct BarSurfaceInputSample
	{
		POINT logical{};
		BarSurfaceButtonId hit = 0;
		bool valid = false, background = false, primaryBackground = false, popupBackground = false;
		std::uint64_t revision = 0;
	};
	struct BarSurfaceRenderResult
	{
		bool rendered = false, animationActive = false, invalidated = false;
		RECT damage{};
	};
	struct BarSurfaceHooks
	{
		std::function<void()> invalidate, wake;
	};
	struct BarSurfaceSharedLighting
	{
		double primaryScreenX = 0, primaryScreenY = 0, primaryRadiusPixels = 0;
		double cursorScreenX = 0, cursorScreenY = 0, cursorRadiusPixels = 0;
		double cursorIntensity = 0;
		COLORREF drawingPenColor = RGB(0, 0, 0);
		double drawingPenColorBlend = 0, drawingLightOpacity = 1;
		bool primaryVisible = false, cursorVisible = false, edgeLightingEnabled = false;
	};

	// 这里只保留分页的数据适配；按钮、输入、动画和渲染事务由主栏公共函数执行。
	// More/Theme 是两个固定属性面板，不是可递归的 UI 节点图。
	class BarPageSurface
	{
	public:
		BarPageSurface();
		~BarPageSurface();
		BarPageSurface(const BarPageSurface&) = delete;
		BarPageSurface& operator=(const BarPageSurface&) = delete;
		BarPageSurface(BarPageSurface&&) noexcept;
		BarPageSurface& operator=(BarPageSurface&&) noexcept;
		bool ConfigurePagination(const BarPageSurfaceContent&, double durationSeconds = 0);
		bool ConfigureBackground(const BarSurfaceBackgroundSpec&);
		bool SetBounds(RECT, float) noexcept;
		void SetFontTargetScale(double) noexcept;
		void SetOpacity(double, double durationSeconds = 0.2) noexcept;
		void SetHooks(BarSurfaceHooks);
		void SetSharedLightingSubscribed(bool) noexcept;
		static void PublishSharedLighting(const BarSurfaceSharedLighting&) noexcept;
		void Reset() noexcept;
		void ReleaseDeviceResources() noexcept;
		void Invalidate() noexcept;
		[[nodiscard]] bool IsInvalidated() const noexcept;
		[[nodiscard]] RECT PendingDamage() const noexcept;
		[[nodiscard]] RECT ConsumeDamage() noexcept;
		[[nodiscard]] BarPageSurfaceLayout Layout() const;
		[[nodiscard]] BarPageSurfaceGeometry Geometry() const noexcept;
		[[nodiscard]] RECT LogicalBounds() const noexcept;
		[[nodiscard]] RECT PresentationBounds() const noexcept;
		[[nodiscard]] LONG PresentationOutsetPixels() const noexcept;
		[[nodiscard]] HRESULT EnsureDeviceResources(const Inkeys::UI::RenderPipeline::DeviceEpoch&, UINT, UINT);
		[[nodiscard]] ID2D1DeviceContext* DeviceContext() const noexcept;
		[[nodiscard]] ID2D1GdiInteropRenderTarget* GdiInteropRenderTarget() const noexcept;
		[[nodiscard]] BarUIRendering& Renderer() noexcept;
		void HandleFrameEndDrawResult(HRESULT);
		// 输入仅查询上次成功提交的快照，候选帧失败不能提前移动命中区域。
		void CommitPresented(bool inputEnabled = true) noexcept;
		// 固定候选快照只保留最新帧；异步原生回执不能确认后来推进的命中或清除新 damage。
		[[nodiscard]] std::uint64_t CapturePresentationCandidate() noexcept;
		[[nodiscard]] bool IsPresentationCandidateCurrent(std::uint64_t serial) const noexcept;
		[[nodiscard]] bool CommitPresentationCandidate(std::uint64_t serial,
			bool inputEnabled = true) noexcept;
		[[nodiscard]] std::uint64_t PresentedGeneration() const noexcept;
		[[nodiscard]] std::optional<POINT> PresentationToLogical(POINT) const noexcept;
		[[nodiscard]] bool HitTestBackground(POINT) const noexcept;
		[[nodiscard]] BarSurfaceButtonId HitTest(POINT) const noexcept;
		[[nodiscard]] BarSurfaceButtonId HitTestPresentation(POINT) const noexcept;
		[[nodiscard]] BarSurfaceInputSample SamplePointer(POINT) const noexcept;
		BarSurfacePointerResult ApplyPointer(BarSurfacePointerEventKind,BarSurfaceButtonId,
			std::optional<POINT> screenPoint = std::nullopt) noexcept;
		BarSurfacePointerResult PointerMove(POINT) noexcept;
		BarSurfacePointerResult PointerLeave() noexcept;
		BarSurfacePointerResult PointerDown(POINT) noexcept;
		BarSurfacePointerResult PointerUp(POINT, bool invokeCallback = false) noexcept;
		BarSurfacePointerResult CancelPointer() noexcept;
		[[nodiscard]] bool Advance(std::chrono::steady_clock::time_point) noexcept;
		void SuspendForIdle(std::chrono::steady_clock::time_point) noexcept;
		[[nodiscard]] bool AnimationActive() const noexcept;
		[[nodiscard]] bool PopupAnimationActive() const noexcept;
		BarSurfaceRenderResult Render(ID2D1DeviceContext*, std::chrono::steady_clock::time_point);
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
}
