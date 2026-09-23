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
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string_view>

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
		PresentLockWait, Draw, GetDC, ULW, ReleaseDC, EndDraw, Count,
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

	// 仅当前渲染回调写入；无 sink / 非调度上下文时访问器返回 nullptr。
	struct FrameDiagnostics
	{
		bool barSampled = false, animationAdvanced = false;
		bool presentAttempted = false, ulwAttempted = false, ulwSucceeded = false, presentCommitted = false;
		bool presentDeferred = false, backoffSkipped = false;
		bool failureRecoveryReset = false, presentFailed = false;
		bool callbackException = false;
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
}
