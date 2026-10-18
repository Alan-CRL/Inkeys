module;

#include <windows.h>

#include <d2d1_1.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dwrite_1.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

export module Inkeys.UI.RenderPipeline;

export namespace Inkeys::UI::RenderPipeline
{
	using Microsoft::WRL::ComPtr;

	enum class Backend : std::uint8_t
	{
		Warp,
		Hardware,
	};

	struct DeviceEpoch
	{
		Backend backend = Backend::Warp;
		std::uint64_t generation = 0;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		ComPtr<ID3D11Device> d3dDevice;
		ComPtr<ID3D11Device1> d3dDevice1;
		ComPtr<ID3D11DeviceContext> immediateContext;
		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIFactory> dxgiFactory;
		ComPtr<ID2D1Device> d2dDevice;
	};

	struct SharedAssets
	{
		ComPtr<ID2D1Factory1> d2dFactory;
		ComPtr<IDWriteFactory1> dwriteFactory;
		ComPtr<IDWriteFontCollection> fontCollection;
	};

	struct FrameContext
	{
		DeviceEpoch epoch;
		SharedAssets assets;
		std::chrono::steady_clock::time_point frameTime{};
	};

	enum class Client : std::uint8_t
	{
		Bar,
		StartupPreview,
		PptBottomLeft,
		PptBottomRight,
		PptMiddleLeft,
		PptMiddleRight,
		Settings,
		WhiteboardFreeze,
		Count,
	};

	enum class FrameResult : std::uint8_t
	{
		Idle,
		Continue,
		Retry,
		DeviceLost,
		// 仅用于进程级管线退出；局部客户端停止应调用 Unregister。
		Stop,
	};

	enum class FrameStage : std::uint8_t
	{
		PresentLockWait, Draw, GetDC, ULW, ReleaseDC, EndDraw,
		// 原六项序号保留；Resources 是 DirtyAndPrepare 的包含子段。
		WakeAndSnapshot, DisplayTransition, SubmitTargetsAndLayout,
		AdvanceAnimationsAndDeriveLayout, PrepareLightingAndDemand,
		DirtyAndPrepare, Resources, Count,
	};

	enum class ExactFallback : std::uint8_t
	{
		Transform, SizeOrBudget, Warming, CreateFailure, Other, Unavailable,
		GeometryScale, QuantizedRadius, Dpi, Alignment, Count,
	};

	struct LightingDiagnostics
	{
		std::uint64_t roundedParentHit = 0, roundedParentMiss = 0;
		std::uint64_t roundedParentCreate = 0, roundedParentFailure = 0;
		std::uint64_t geometryParentHit = 0, geometryParentMiss = 0;
		std::uint64_t geometryParentCreate = 0, geometryParentFailure = 0;
		double roundedParentCreateMs = 0.0, geometryParentCreateMs = 0.0;
		std::uint64_t exactHit = 0, slices = 0;
		std::array<std::uint64_t, static_cast<std::size_t>(ExactFallback::Count)>
			exactFallback{};
	};

	// 仅当前渲染回调写入；无诊断消费者 / 非调度上下文时访问器返回 nullptr。
	struct FrameDiagnostics
	{
		bool barSampled = false, animationAdvanced = false;
		bool presentAttempted = false, ulwAttempted = false, ulwSucceeded = false, presentCommitted = false;
		bool presentDeferred = false, backoffSkipped = false;
		bool failureRecoveryReset = false, presentFailed = false;
		bool callbackException = false;
		// 只由真正 raw capture 启用；旧异常 sink/TLS 不授权新七阶段与真戳。
		bool detailedCaptureEnabled = false;
		// 四阶段成功事务的软件确认时刻；不表示光学像素可见。
		bool hasBarCommitStamp = false;
		std::int64_t barCommitTicks = 0;
		std::uint64_t barAttemptSerial = 0, barCommitEpoch = 0;
		// 原回调/退避序号，不作为成功次数。
		std::uint64_t presentAttemptFrameSerial = 0;
		double rawDtSeconds = 0.0, animationDtSeconds = 0.0;
		std::array<double, static_cast<std::size_t>(FrameStage::Count)> stageMs{};
		HRESULT resourceResult = S_OK, getDcResult = S_OK;
		HRESULT releaseDcResult = S_OK, endDrawResult = S_OK;
		DWORD ulwError = 0;
		std::uint32_t failureCount = 0;
		std::uint64_t retryDelayFrames = 0, nextRetryFrame = 0;
		std::uint64_t epoch = 0;
		Backend backend = Backend::Warp;
		SIZE targetSize{}, capacitySize{};
		RECT viewport{};
		POINT source{};
		double displayCapacityZoom = 0.0, zoom = 0.0;
		// bit0: 边缘光开关，bit1: 主光可见，bit2: 鼠标光可见，bit3: 鼠标光有强度。
		std::uint32_t lightFlags = 0;
		LightingDiagnostics light;
	};

	using BarCommitClock = std::int64_t (*)() noexcept;
	// 默认读 steady_clock；可选时钟只供无窗口合同测试，nullptr/失败/重复戳均不读钟。
	void StampBarCommit(FrameDiagnostics* diagnostics, bool committed,
		std::uint64_t attemptSerial, std::uint64_t epoch,
		BarCommitClock clock = nullptr) noexcept;

	class FrameStageTimer
	{
	public:
		FrameStageTimer(FrameDiagnostics* diagnostics, FrameStage stage) noexcept;
		~FrameStageTimer();
		FrameStageTimer(const FrameStageTimer&) = delete;
		FrameStageTimer& operator=(const FrameStageTimer&) = delete;
		void Stop() noexcept;

	private:
		FrameDiagnostics* diagnostics_;
		FrameStage stage_;
		std::chrono::steady_clock::time_point start_{};
	};

	[[nodiscard]] FrameDiagnostics* CurrentFrameDiagnostics() noexcept;
	// false / 异常表示暂未接收，聚合保留到下一次限频窗口；调用发生在内部锁外。
	using DiagnosticsSink = std::function<bool(std::string_view)>;

	using ClientMask = std::uint32_t;
	using RenderCallback = std::function<FrameResult(const FrameContext&)>;
	using ContextProvider = std::function<FrameContext(std::chrono::steady_clock::time_point)>;
	using DeviceRecoveryCallback = std::function<bool()>;
	using ControlCallback = std::function<bool()>;
	using ControlTask = std::function<void()>;

	enum class BarCommitStampStatus : std::uint8_t { Absent, Unverified, Valid, Invalid };

	struct RawCallbackSample
	{
		std::uint64_t runSerial = 0, batchSerial = 0, callbackSerial = 0;
		Client client = Client::Bar;
		std::uint64_t callbackGeneration = 0, contextEpoch = 0;
		Backend backend = Backend::Warp;
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
		std::uint64_t schedulerIdleEpoch = 0, activitySegment = 0;
		FrameResult result = FrameResult::Idle;
		bool requested = false, continued = false, retried = false;
		bool validTime = true, hasPreviousActiveCallback = false, hasPreviousActiveCommit = false;
		std::int64_t frameTimeTicks = 0, startTicks = 0, endTicks = 0;
		std::int64_t previousActiveCallbackTicks = 0, previousActiveCommitTicks = 0;
		BarCommitStampStatus barCommitStatus = BarCommitStampStatus::Absent;
		bool hasPreviousTrueBarCommit = false;
		std::int64_t previousTrueBarCommitTicks = 0;
		std::uint64_t barSuccessSerial = 0;
		FrameDiagnostics frame;
	};

	struct RawBatchSample
	{
		std::uint64_t runSerial = 0, batchSerial = 0;
		std::int64_t frameTimeTicks = 0, beginTicks = 0, endTicks = 0;
		std::int64_t recoveryStartTicks = 0, recoveryEndTicks = 0;
		ClientMask work = 0, requested = 0, continued = 0, retried = 0;
		ClientMask registered = 0, executed = 0;
		std::uint64_t contextEpoch = 0;
		bool contextValid = false, recoveryAttempted = false, recoverySucceeded = false;
		bool deviceLost = false, stopped = false, validTime = true;
	};

	struct RawClientCounters
	{
		std::uint64_t seen = 0, advanced = 0, attempts = 0, commits = 0;
		std::uint64_t failures = 0, idle = 0, retries = 0;
		std::uint64_t trueBarCommits = 0, unverifiedBarCommits = 0, invalidBarCommitStamps = 0;
	};

	// 原始 tick 属于 steady_clock；旧链是 callback-end 代理，真 Bar 事务另存软件确认戳。
	struct RawCaptureReport
	{
		std::uint32_t schemaVersion = 2;
		std::uint64_t runSerial = 0;
		std::size_t capacity = 0, allocatedBytes = 0;
		std::int64_t clockPeriodNum = std::chrono::steady_clock::period::num;
		std::int64_t clockPeriodDen = std::chrono::steady_clock::period::den;
		std::int64_t originTicks = 0;
		bool sealed = false;
		std::uint64_t callbackSeen = 0, callbackRetained = 0, callbackDropped = 0;
		std::uint64_t batchSeen = 0, batchRetained = 0, batchDropped = 0;
		std::uint64_t invalidCallbacks = 0, invalidBatches = 0, idleTransitions = 0;
		std::uint64_t trueBarCommits = 0, unverifiedBarCommits = 0, invalidBarCommitStamps = 0;
		std::array<RawClientCounters, static_cast<std::size_t>(Client::Count)> clients{};
		std::vector<RawCallbackSample> callbacks;
		std::vector<RawBatchSample> batches;
	};

	enum class RawCaptureTestPoint : std::uint8_t { AfterAllocation, BeforeRelease };
	using RawCaptureTestHook = void (*)(RawCaptureTestPoint, void*);

	[[nodiscard]] constexpr ClientMask Mask(Client client) noexcept
	{
		return ClientMask{ 1 } << static_cast<unsigned>(client);
	}

	[[nodiscard]] constexpr ClientMask PptPageMask() noexcept
	{
		return Mask(Client::PptBottomLeft) | Mask(Client::PptBottomRight)
			| Mask(Client::PptMiddleLeft) | Mask(Client::PptMiddleRight);
	}

	[[nodiscard]] constexpr ClientMask PptMask() noexcept
	{
		return PptPageMask();
	}

	[[nodiscard]] constexpr ClientMask WhiteboardMask() noexcept
	{
		return Mask(Client::WhiteboardFreeze)
			| Mask(Client::PptBottomLeft) | Mask(Client::PptBottomRight);
	}

	struct DispatchDecision
	{
		ClientMask work = 0;
		ClientMask next = 0;
		ClientMask requested = 0;
		bool sleep = true;
		bool rebuildSharedDevice = false;
		bool stop = false;
	};

	class DispatchState
	{
	public:
		void Request(ClientMask mask) noexcept;
		[[nodiscard]] ClientMask TakeRequested() noexcept;
		void Reset() noexcept;
		[[nodiscard]] DispatchDecision Complete(
			ClientMask work,
			ClientMask registered,
			const std::array<FrameResult, static_cast<std::size_t>(Client::Count)>&
				results) noexcept;

	private:
		std::atomic<ClientMask> requested_ = 0;
	};

	// Scheduler 保持可独立实例化，供无窗口测试验证唤醒和节拍合同。
	class Scheduler
	{
	public:
		Scheduler();
		~Scheduler();
		Scheduler(const Scheduler&) = delete;
		Scheduler& operator=(const Scheduler&) = delete;

		[[nodiscard]] bool Start(
			ContextProvider contextProvider = {},
			DeviceRecoveryCallback deviceRecovery = {},
			ControlCallback controlCallback = {});
		void Stop() noexcept;
		[[nodiscard]] bool Register(Client client, RenderCallback callback);
		// 返回时该客户端已没有正在执行的回调；不得从该客户端自己的回调中调用。
		void Unregister(Client client) noexcept;
		void Request(Client client) noexcept;
		void Request(ClientMask mask) noexcept;
		void RequestControl() noexcept;
		[[nodiscard]] bool PostControl(ControlTask task);
		void WakeForStop() noexcept;
		// 可在 Start 前或运行中注册；Stop drain 后释放，不影响其他 Scheduler。
		[[nodiscard]] bool SetDiagnosticsSink(DiagnosticsSink sink);
		// 只在停止且渲染线程已 join 后配置；0 禁用下一轮，报告仍可在 Stop 后取走。
		[[nodiscard]] bool ConfigureRawCapture(std::size_t capacity = 32768);
		[[nodiscard]] std::optional<RawCaptureReport> TakeRawCapture() noexcept;
		// 默认空；仅无窗口竞态测试在非回调线程使用。
		void SetRawCaptureTestHookForTests(RawCaptureTestHook hook, void* context) noexcept;

	private:
		struct Impl;
		Impl* impl_ = nullptr;
	};

	HRESULT Initialize();
	void Shutdown() noexcept;
	[[nodiscard]] bool IsInitialized() noexcept;
	HRESULT InitializeFontCollection(
		IDWriteFontFileLoader* fileLoader,
		IDWriteFontCollectionLoader* collectionLoader,
		std::span<const UINT> resourceIds);

	[[nodiscard]] DeviceEpoch GetDeviceEpoch();
	[[nodiscard]] SharedAssets GetSharedAssets();
	[[nodiscard]] ComPtr<ID2D1Factory1> D2DFactory();
	[[nodiscard]] ComPtr<IDWriteFactory1> DWriteFactory();
	[[nodiscard]] ComPtr<IDWriteFontCollection> FontCollection();

	HRESULT PrepareBackend(Backend backend);
	bool CommitPreparedBackend() noexcept;

	[[nodiscard]] bool Register(Client client, RenderCallback callback);
	void Unregister(Client client) noexcept;
	void Request(Client client) noexcept;
	void Request(ClientMask mask) noexcept;
	[[nodiscard]] bool PostControl(ControlTask task);
	void WakeForStop() noexcept;
	[[nodiscard]] bool SetDiagnosticsSink(DiagnosticsSink sink);
	[[nodiscard]] bool ConfigureRawCapture(std::size_t capacity = 32768);
	[[nodiscard]] std::optional<RawCaptureReport> TakeRawCapture() noexcept;
}
