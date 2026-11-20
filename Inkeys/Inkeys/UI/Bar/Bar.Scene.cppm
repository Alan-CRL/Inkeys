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
#include "../../Drawing/Draw3/Draw3.ShapeInteraction.h"

export module Inkeys.UI.Bar:Scene;
export import Inkeys.UI.Bar.Metrics;
export import Inkeys.UI.Bar.SurfaceLayout;
import Inkeys.UI.UI3;
import Inkeys.UI.RenderPipeline;

using namespace Inkeys::UI::UI3;

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
		// 相对背景左上角的 DIP 包络及动画声明；省略时保持既有保守绘制，调用方负责脏区。
		std::optional<Inkeys::UI::UI3::ContentExtent> contentExtent;
	};

	using BarSurfaceButtonId = std::uint32_t;
	inline constexpr BarSurfaceButtonId BarSurfaceNoButton = 0;
	enum class BarPageContentKind : std::uint8_t { Numeric, Status };
	// 与 PageLoadState 数值一致；None 以外的状态都先经过同一个 500ms 门控才显示文字/变浅/环。
	enum class BarPageLoadState : std::uint8_t { None, Loading, RetryableFailed, FinalFailed };
	struct BarPageSurfaceContent
	{
		BarPageContentKind kind = BarPageContentKind::Numeric;
		BarPageLoadState loadState = BarPageLoadState::None;
		unsigned surface = 0;
		std::uint64_t loadingSession = 0;
		bool whiteboard = false, switching = false, pptActive = false;
		// 小尺寸档位：箭头/加页/更多 1×1、页码 2×1（竖向 1×2），外框短边 42.5，不显示按钮文字与状态词。
		bool compact = false;
		bool nextIsAdd = false, nextIsEndShow = false, unsaved = false;
		bool navigationWaitingPrevious = false, navigationWaitingNext = false;
		int menu = 0;
		std::array<bool, 3> enabled{ true, true, true };
		std::array<bool, 3> interactive{ true, true, true };
		// currentPage/totalPage 恒为常态页码；statusPrimary/statusSecondary 是门控到点后替换它们的状态文字，空串表示不替换该槽。
		std::wstring currentPage, totalPage, statusPrimary, statusSecondary;
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
		void SetIdleFade(bool dimmed, double durationSeconds) noexcept;
		void SetHooks(BarSurfaceHooks);
		void SetSharedLightingSubscribed(bool) noexcept;
		void Reset() noexcept;
		// 帧目标由宿主持有并驱动（Ensure、帧事务）；场景只把内部绘制器绑定上去，传 nullptr 解绑。
		// 一个帧目标同一时刻只绑定一个场景；帧目标须先于场景析构（声明在场景之后）或先解绑。
		void BindFrameTarget(FrameTarget*);
		// 宿主在 Ensure 帧目标之前调用：所绑定帧目标的设备代次将变化时，先清场景自有的设备资源。
		void PrepareDeviceGeneration(unsigned long long generation) noexcept;
		// 设备缓存重置：按固定先后清场景自有设备资源并丢弃所绑定的帧目标，随后整窗失效。
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
		// 输入仅查询上次成功提交的快照，候选帧失败不能提前移动命中区域。
		void CommitPresented(bool inputEnabled = true) noexcept;
		// 固定候选快照只保留最新帧；异步原生回执不能确认后来推进的命中或清除新 damage。
		[[nodiscard]] std::uint64_t CapturePresentationCandidate() noexcept;
		[[nodiscard]] bool IsPresentationCandidateCurrent(std::uint64_t serial) const noexcept;
		// pixelsSubmitted=false：隐藏或无像素候选，只确认窗口与输入，不推进脏区登记表的像素快照。
		[[nodiscard]] bool CommitPresentationCandidate(std::uint64_t serial,
			bool inputEnabled = true, bool pixelsSubmitted = true) noexcept;
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
		// 仅字号修正动画；宿主在本帧 Render 之后据此决定是否续帧。
		[[nodiscard]] bool FontAnimationsActive() const noexcept;
		[[nodiscard]] bool PopupAnimationActive() const noexcept;
		void SetLoadingIndicatorVisible(bool) noexcept;
		[[nodiscard]] bool LoadingIndicatorAnimationActive() const noexcept;
		[[nodiscard]] std::chrono::steady_clock::time_point LoadingIndicatorDeadline() const noexcept;
		BarSurfaceRenderResult Render(ID2D1DeviceContext*, std::chrono::steady_clock::time_point);
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
	// 当前对象编辑场景：窗口/输入适配不拥有按钮、动画或文档。
	struct ShapeEditorHit
	{
		bool occupied = false;
		std::uint32_t handle = 0, button = 0;
		bool body = false;
		Inkeys::Drawing::Draw3::ShapeUiIdentity identity;
	};
	bool SubmitShapeUiIntent(const Inkeys::Drawing::Draw3::ShapeUiAction&) noexcept;
	std::chrono::steady_clock::time_point RetryShapeUiIntents(std::chrono::steady_clock::time_point) noexcept;
	void ClearShapeUiIntents() noexcept;
	int RunShapeEditorOffscreenTest();
	// 仅供迁移像素基线使用：0 工具栏、1 色板、2 边数、3/4 亮/暗色系取色器。
	HRESULT RenderShapeEditorGoldenScene(unsigned layer,
		const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>& present);
	class ShapeEditorSurface
	{
	public:
		ShapeEditorSurface();
		~ShapeEditorSurface();
		void Configure(std::shared_ptr<const Inkeys::Drawing::Draw3::ShapeUiSnapshot>, RECT workArea, RECT barBounds);
		bool Advance(std::chrono::steady_clock::time_point);
		bool Visible() const noexcept;
		bool AnimationActive() const noexcept;
		RECT Bounds() const noexcept;
		// 帧目标与帧事务由宿主持有并驱动；场景只把内部绘制器绑定上去，传 nullptr 解绑。
		// 帧目标须先于场景析构或先解绑；场景析构不访问帧目标。
		void BindFrameTarget(FrameTarget*);
		// 宿主在 Ensure 帧目标之前调用：设备代次变化时先清场景自有的设备资源。
		void PrepareDeviceGeneration(unsigned long long generation);
		// 内容绘制：宿主已开始帧事务，传入所绑定帧目标的设备上下文。
		void Draw(ID2D1DeviceContext*);
		void CommitPresented(bool inputEnabled = true);
		// 自上次成功提交后输入快照、布局、动画或光照有变化才需要回读与 ULW。
		bool NeedsPresent() const noexcept;
		ShapeEditorHit Hit(POINT screen) const;
		bool PreservePointerTrajectory(POINT down) const;
		bool Pointer(UINT message, POINT screen, bool captureOwned = true, std::uint64_t expectedPresentation = 0, short wheelDelta = 0);
		bool Contact(UINT message, POINT screen, std::uint64_t source, std::uint32_t contactId,
			bool touch, bool pen, double seconds, bool captureOwned, std::uint64_t presentedRevision);
		std::chrono::steady_clock::time_point TickPrediction(std::chrono::steady_clock::time_point);
		std::chrono::steady_clock::time_point TickToolbarIdle(std::chrono::steady_clock::time_point);
		std::uint64_t PresentedGeneration() const;
		void CancelPointer();
		// 设备缓存重置：先丢弃所绑定的帧目标，再清场景自有设备资源。
		void ReleaseDeviceResources();
		// 离屏测试与生产输入共用同一动作出口；不改业务状态。
		void SetActionSink(std::function<void(const Inkeys::Drawing::Draw3::ShapeUiAction&)>);
		friend int RunShapeEditorOffscreenTest();
		friend HRESULT RenderShapeEditorGoldenScene(unsigned layer,
			const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>& present);
	private:
		struct Impl;
		std::unique_ptr<Impl> impl_;
	};
	// 形状编辑一帧的呈现事务（清变换 → 帧事务开始 → Draw → GetDC → 提交端 → ReleaseDC → EndDraw）。
	// 帧目标由宿主持有并已 Ensure；提交端由宿主给出（生产为 ULW，离屏测试为读回）。生产与测试共用这一份，
	// 返回口径不变：EndDraw → GetDC → ReleaseDC 的失败码依次优先，否则按提交端是否成功返回 S_OK / E_FAIL。
	// log 非空时把事务结果报给日志闩锁（role 为静态字符串）。
	HRESULT PresentShapeEditorFrame(ShapeEditorSurface& content, FrameTarget& target,
		const std::function<BOOL(const UPDATELAYEREDWINDOWINFO&)>& sink,
		FrameTransactionLogState* log = nullptr, const char* role = nullptr);

}
