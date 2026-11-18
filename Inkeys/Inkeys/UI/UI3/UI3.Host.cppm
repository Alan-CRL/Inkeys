module;

#include <windows.h>

#include <d2d1_1.h>
#include <d2d1helper.h>
#include <wrl/client.h>
#include <cstdint>
#include <functional>

export module Inkeys.UI.UI3:Host;

import :Renderer;

import Inkeys.UI.RenderPipeline;

using Microsoft::WRL::ComPtr;

namespace Inkeys::UI::UI3
{
// 帧目标形态：决定目标资源由谁创建，以及帧目标是否负责 Begin/End/Clear。
export enum class FrameTargetKind : std::uint8_t
{
	// TARGET | GDI_COMPATIBLE、BGRA8 预乘位图 + GDI interop；现有 ULW 宿主使用。
	GdiCompatibleBitmap,
	// 仅 TARGET 的离屏位图，没有 GDI interop。
	PlainBitmap,
	// 调用方提供已处于 BeginDraw 状态的设备上下文；帧目标不创建资源、不 Begin/End/Clear。
	ExternalContext,
};

// 帧目标：拥有设备上下文、绘制目标与帧生命周期；内容绘制器只经通告入口得知设备与帧状态。
export class FrameTarget
{
public:
	explicit FrameTarget(FrameTargetKind kind = FrameTargetKind::GdiCompatibleBitmap) noexcept
		: kind_(kind) {}
	explicit FrameTarget(UIRendering& renderer,
		FrameTargetKind kind = FrameTargetKind::GdiCompatibleBitmap) noexcept
		: renderer_(&renderer), kind_(kind) {}
	// 绑定的绘制器以地址引用，帧目标不可复制或移动。
	FrameTarget(const FrameTarget&) = delete;
	FrameTarget& operator=(const FrameTarget&) = delete;

	// 改绑绘制器：旧绘制器按「目标已丢弃」清理，新绘制器立即得知现有资源
	// （新绘制器若还带着别的上下文或设备代次的缓存，先清理再通告）。
	void BindRenderer(UIRendering* renderer);
	[[nodiscard]] UIRendering* BoundRenderer() const noexcept { return renderer_; }
	[[nodiscard]] FrameTargetKind Kind() const noexcept { return kind_; }
	// 外部上下文形态下是否由帧目标推入脏区裁剪（默认推入）；位图形态恒推入。
	// 内容绘制不保证自己限制在视口内（柔和阴影会画到视口之外）：关闭后必须由宿主自己裁到视口。
	void SetExternalDirtyClipEnabled(bool enabled) noexcept { externalDirtyClipEnabled_ = enabled; }

	// 位图形态：设备代次、资源缺失或像素尺寸变化时重建；新资源全部成功后才替换旧资源。
	HRESULT Ensure(const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch,
		UINT32 targetWidth, UINT32 targetHeight);
	// 外部上下文形态：上下文或设备代次变化时按「丢弃 → 就绪」通告绘制器；尺寸只作记录。
	HRESULT AttachExternalContext(ID2D1DeviceContext* context,
		unsigned long long generation, UINT32 targetWidth, UINT32 targetHeight);
	void Discard();

	// 位图形态先推入脏区裁剪再清空（裁剪先于清空）；外部上下文形态只在开启时推入裁剪、不清空。
	// GetDC 之前必须结束 clip/layer 栈。
	void BeginFrame(ID2D1DeviceContext* context, const D2D1_RECT_F& dirtyRect);
	void FinishDrawCommands(ID2D1DeviceContext* context);
	// 位图形态执行 EndDraw 并回调绑定绘制器；外部上下文形态不 EndDraw，见 NotifyExternalEndDraw。
	HRESULT EndFrame(ID2D1DeviceContext* context);
	// 外部上下文形态：宿主自行 EndDraw 后把结果交回，供绘制器处理遮罩失败与帧尾清理。
	void NotifyExternalEndDraw(HRESULT endDrawResult);
	void PushDirtyClip(ID2D1DeviceContext* context, const D2D1_RECT_F& dirtyRect);
	void PopDirtyClip(ID2D1DeviceContext* context);

	[[nodiscard]] ID2D1DeviceContext* DeviceContext() const noexcept { return deviceContext_.Get(); }
	[[nodiscard]] ID2D1GdiInteropRenderTarget* GdiInteropRenderTarget() const noexcept
	{ return gdiInteropRenderTarget_.Get(); }
	[[nodiscard]] ID2D1Bitmap1* TargetBitmap() const noexcept { return targetBitmap_.Get(); }
	[[nodiscard]] unsigned long long DeviceGeneration() const noexcept { return deviceGeneration_; }
	[[nodiscard]] D2D1_SIZE_U PixelSize() const noexcept
	{ return D2D1::SizeU(pixelWidth_, pixelHeight_); }
	[[nodiscard]] bool DirtyClipActive() const noexcept { return dirtyClipActive_; }
	[[nodiscard]] const D2D1_RECT_F& DirtyClipRect() const noexcept { return dirtyClipRect_; }

private:
	HRESULT Recreate(const Inkeys::UI::RenderPipeline::DeviceEpoch& epoch,
		UINT32 targetWidth, UINT32 targetHeight);

	UIRendering* renderer_ = nullptr;
	FrameTargetKind kind_ = FrameTargetKind::GdiCompatibleBitmap;
	bool externalDirtyClipEnabled_ = true;
	ComPtr<ID2D1DeviceContext> deviceContext_;
	ComPtr<ID2D1Bitmap1> targetBitmap_;
	ComPtr<ID2D1GdiInteropRenderTarget> gdiInteropRenderTarget_;
	unsigned long long deviceGeneration_ = 0;
	// 重建失败日志闩锁：每设备代次、每 API 一次；资源丢弃不清。
	unsigned long long failureGeneration_ = 0;
	unsigned int failureMask_ = 0;
	UINT32 pixelWidth_ = 0;
	UINT32 pixelHeight_ = 0;
	bool dirtyClipActive_ = false;
	D2D1_RECT_F dirtyClipRect_{};
};

// 提交端收到的像素来源。GDI 兼容位图目标提供 GetDC(COPY) 取得的源 DC；
// 其余形态没有 DC，位图形态提供目标位图供读回类提交端使用。
export struct FramePresentSource
{
	HDC sourceDc = nullptr;
	ID2D1Bitmap1* targetBitmap = nullptr;
	D2D1_SIZE_U pixelSize{};
};
export struct FramePresentOutcome
{
	bool succeeded = false;
	DWORD error = ERROR_SUCCESS;
};
// 提交端由宿主提供：ULW、Freeze 表面提交、测试读回等。
export using FramePresentSink = std::function<FramePresentOutcome(const FramePresentSource&)>;

// 帧事务逐步结果；事务只如实返回，是否确认成功及推进何种状态由宿主决定。
export struct FrameTransactionResult
{
	bool commitRequested = false;    // 经 Commit 结束；EndWithoutPresent（延后/放弃本帧）为 false
	HRESULT getDc = E_POINTER;       // GetDC(COPY)；没有 GDI interop 的形态记 S_OK
	// GetDC 报告成功却没有给出 DC：getDc 已改记 E_POINTER；沿用其他失败码的宿主据此自行转换。
	bool sourceDcMissing = false;
	bool presentAttempted = false;   // 提交端是否被调用
	bool presentSucceeded = false;
	DWORD presentError = ERROR_SUCCESS;
	// 取到源 DC 后才会 ReleaseDC；未执行时 releaseDc 保持 E_FAIL。
	bool releaseDcAttempted = false;
	HRESULT releaseDc = E_FAIL;      // ReleaseDC(空修改矩形)；没有 GDI interop 的形态记 S_OK
	HRESULT endDraw = E_FAIL;        // EndDraw；外部上下文形态由宿主自行结束，记 S_OK
	// 四步全部成功才算像素提交成功。
	[[nodiscard]] bool PixelsCommitted() const noexcept
	{
		return SUCCEEDED(getDc) && presentSucceeded && SUCCEEDED(releaseDc) && SUCCEEDED(endDraw);
	}
};

// Commit 中由事务执行的步骤；提交端（第二步）是宿主自己的回调，由宿主在其中自行计时。
export enum class FrameCommitStep : std::uint8_t { GetDC, ReleaseDC, EndDraw };
// 分步观察：每一步执行前（begin=true）与执行后各回调一次，供宿主计时与诊断；
// 回调不得改变帧状态。没有执行的步骤不回调。
export using FrameCommitStepObserver = std::function<void(FrameCommitStep step, bool begin)>;

// 帧事务：「Begin → 绘制 → Finish → GetDC → 提交 → ReleaseDC → End」的单次成对执行。
// 构造即开始（裁剪先于清空）；宿主在 FinishDrawCommands 与 Commit 之间完成提交准备。
export class FrameTransaction
{
public:
	FrameTransaction(FrameTarget& target, const D2D1_RECT_F& dirtyRect);
	FrameTransaction(const FrameTransaction&) = delete;
	FrameTransaction& operator=(const FrameTransaction&) = delete;
	// 尚未结束的事务按「不提交」成对收尾，保证 Begin/End 单次配对。
	~FrameTransaction();

	// 目标资源齐备且尚未结束。
	[[nodiscard]] bool Active() const noexcept { return context_ && !ended_; }
	// 绘制阶段交给内容的设备上下文；事务持有它到结束。
	[[nodiscard]] ID2D1DeviceContext* DeviceContext() const noexcept { return context_.Get(); }
	// 退出裁剪/图层栈；GetDC 之前必须完成，可重复调用。
	void FinishDrawCommands();
	// GDI 兼容位图：GetDC(COPY) → 提交端 → ReleaseDC(空修改矩形) → EndDraw。
	// 普通位图：EndDraw 成功后把目标位图交给提交端。外部上下文：只调用提交端。
	// 没有 GDI interop 的形态允许空提交端（宿主自行呈现）。
	// 渲染线程上的宿主以 std::ref 传入栈上的可调用对象，避免逐帧堆分配。
	FrameTransactionResult Commit(const FramePresentSink& sink,
		const FrameCommitStepObserver& observer = {});
	// 延后或放弃本帧：不取 DC、不调用提交端，只成对结束。
	FrameTransactionResult EndWithoutPresent();

private:
	FrameTarget& target_;
	ComPtr<ID2D1DeviceContext> context_;
	ComPtr<ID2D1GdiInteropRenderTarget> gdiInterop_;
	bool commandsFinished_ = false;
	bool ended_ = false;
};

// 帧事务失败/恢复的日志闩锁：随宿主持有，各类宿主共用。
// 首次失败记一条 Error（四步各自的结果码），持续失败不重复；恢复时记一条 Info（带连续失败次数）。
export class FrameTransactionLogState
{
public:
	// 每次事务结束后报告一次；role 由宿主给出（静态字符串）。
	// 延后/放弃本帧（EndWithoutPresent）只要 EndDraw 成功就既不算失败、也不算恢复。
	void Report(const char* role, const FrameTransactionResult& result) noexcept;
	[[nodiscard]] unsigned ConsecutiveFailures() const noexcept { return consecutiveFailures_; }

private:
	unsigned consecutiveFailures_ = 0;
};

// 内容布局坐标 ↔ 目标像素 ↔ 屏幕 的映射。宿主每帧提供；绘制、光照局部映射与命中共用同一份。
// 目标像素 = 布局坐标 × layoutScale − targetOrigin；屏幕像素 = 布局坐标 × layoutScale + screenOrigin。
export struct FrameCoordinateMapping
{
	POINT screenOrigin{};      // 布局原点所在的屏幕像素
	double layoutScale = 1.0;  // 布局单位到像素的缩放
	POINT targetOrigin{};      // 目标位图 (0,0) 对应的缩放后布局像素
	LONG lightingOutset = 0;   // 光效相对内容范围的外扩（像素）
};
export
{
	[[nodiscard]] D2D1_POINT_2F LayoutToTarget(const FrameCoordinateMapping& mapping, D2D1_POINT_2F layout) noexcept;
	[[nodiscard]] D2D1_POINT_2F TargetToLayout(const FrameCoordinateMapping& mapping, D2D1_POINT_2F target) noexcept;
	[[nodiscard]] D2D1_POINT_2F LayoutToScreen(const FrameCoordinateMapping& mapping, D2D1_POINT_2F layout) noexcept;
	[[nodiscard]] D2D1_POINT_2F ScreenToLayout(const FrameCoordinateMapping& mapping, D2D1_POINT_2F screen) noexcept;
	[[nodiscard]] POINT TargetToScreen(const FrameCoordinateMapping& mapping, POINT target) noexcept;
	[[nodiscard]] POINT ScreenToTarget(const FrameCoordinateMapping& mapping, POINT screen) noexcept;
	[[nodiscard]] D2D1_RECT_F LayoutToTargetRect(const FrameCoordinateMapping& mapping, const D2D1_RECT_F& layout) noexcept;
	// 把布局坐标直接画进目标的变换（缩放后平移）。
	[[nodiscard]] D2D1_MATRIX_3X2_F LayoutToTargetTransform(const FrameCoordinateMapping& mapping) noexcept;
	// 内容范围（目标像素）按光效外扩后的范围。
	[[nodiscard]] RECT ExpandByLightingOutset(const FrameCoordinateMapping& mapping, const RECT& targetBounds) noexcept;
}

}
