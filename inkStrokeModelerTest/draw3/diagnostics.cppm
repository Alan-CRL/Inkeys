module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <cstdint>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>
#include <windows.h>

export module draw3.diagnostics;

export namespace draw3
{
#if defined(DRAW3_RTS_DIAGNOSTICS)
	// 标识 RTS callback 在哪一个只读诊断检查点结束。
	enum class RtsPacketResult : uint32_t
	{
		NotApplicable,
		Success,
		InvalidArguments,
		StateGateBusy,
		ContextMissing,
		ContextMismatch,
		BindingMissing,
		DecoderMissing,
		DecoderEnsureFailed,
		BindingInsertFailed,
		GenerationMismatch,
		PropertyCountMismatch,
		DecodeFailed,
		PublishDownFailed,
		PublishMoveFailed,
		PublishUpFailed,
		Count
	};

	// 标识绘制线程读取 contact 后到 Stroke Modeler 的诊断结果。
	enum class DrawingInputResult : uint32_t
	{
		DownDequeued,
		DownInitialized,
		DownHandleInvalid,
		DownIgnoredByPolicy,
		StrokeRuntimeUnavailable,
		ModelerResetFailed,
		SnapshotReadSucceeded,
		SnapshotReadFailed,
		SnapshotFiltered,
		ModelerUpdateSucceeded,
		ModelerUpdateFailed,
		ContactRecycled,
		Count
	};

	// 汇总 RTS 初始化调用的实际结果；未执行的步骤保持 E_NOTIMPL。
	struct RtsInitializationTrace
	{
		DWORD currentThreadId = 0;
		DWORD windowThreadId = 0;
		UINT_PTR windowHandle = 0;
		LONG_PTR windowStyle = 0;
		LONG_PTR windowExtendedStyle = 0;
		ULONG_PTR tabletServiceFlags = 0;
		int digitizer = 0;
		int maximumTouches = 0;
		HRESULT coInitializeResult = E_NOTIMPL;
		HRESULT createStylusResult = E_NOTIMPL;
		HRESULT putHwndResult = E_NOTIMPL;
		HRESULT setAllTabletsModeResult = E_NOTIMPL;
		HRESULT desiredPacketDescriptionResult = E_NOTIMPL;
		HRESULT fallbackPacketDescriptionResult = E_NOTIMPL;
		ULONG selectedPacketPropertyCount = 0;
		const char* selectedPacketDescription = "none";
		HRESULT queryStylus2Result = E_NOTIMPL;
		HRESULT disableFlicksResult = E_NOTIMPL;
		HRESULT queryStylus3Result = E_NOTIMPL;
		HRESULT enableMultiTouchResult = E_NOTIMPL;
		HRESULT readMultiTouchResult = E_NOTIMPL;
		BOOL multiTouchEnabled = FALSE;
		HRESULT marshalerResult = E_NOTIMPL;
		HRESULT addPluginResult = E_NOTIMPL;
		HRESULT enableStylusResult = E_NOTIMPL;
		uint32_t dataInterest = 0;
	};

	// RTS 热路径只复制定长标量，停止回调后再统一输出。
	struct RtsCallbackTrace
	{
		const char* eventName = nullptr;
		uint64_t callbackSequence = 0;
		int64_t qpc = 0;
		DWORD threadId = 0;
		uint32_t tabletContextId = 0;
		uint32_t contactId = 0;
		uint32_t deviceType = 0;
		ULONG packetCount = 0;
		ULONG propertyCount = 0;
		LONG rawX = 0;
		LONG rawY = 0;
		float decodedX = 0.0f;
		float decodedY = 0.0f;
		float decodedPressure = -1.0f;
		HRESULT result = S_OK;
		uint32_t dataInterest = 0;
		RtsPacketResult packetResult = RtsPacketResult::NotApplicable;
		uint64_t lifecycleGeneration = 0;
		uint64_t decoderGeneration = 0;
		uint64_t bindingGeneration = 0;
		bool hasRawPosition = false;
		bool decoded = false;
		bool published = false;
		bool publishAttempted = false;
		bool stateGateEntered = false;
		bool bindingAvailable = false;
		bool decoderAvailable = false;
		bool decoderInitiallyAvailable = false;
		bool decoderEnsureAttempted = false;
		bool decoderEnsureSucceeded = false;
	};

	struct RtsCallbackDiagnosticState
	{
		RtsPacketResult packetResult = RtsPacketResult::NotApplicable;
		uint64_t lifecycleGeneration = 0;
		uint64_t decoderGeneration = 0;
		uint64_t bindingGeneration = 0;
		bool publishAttempted = false;
		bool stateGateEntered = false;
		bool bindingAvailable = false;
		bool decoderAvailable = false;
		bool decoderInitiallyAvailable = false;
		bool decoderEnsureAttempted = false;
		bool decoderEnsureSucceeded = false;
	};

	struct DrawingInputTrace
	{
		const char* eventName = nullptr;
		int64_t qpc = 0;
		int64_t snapshotQpc = 0;
		DWORD threadId = 0;
		uint32_t tabletContextId = 0;
		uint32_t contactId = 0;
		uint32_t deviceType = 0;
		uint32_t phase = 0;
		uint64_t contactGeneration = 0;
		uint64_t snapshotSequence = 0;
		float x = 0.0f;
		float y = 0.0f;
		float pressure = -1.0f;
		uint32_t modeledPointCount = 0;
		uint32_t realPointCount = 0;
		DrawingInputResult result = DrawingInputResult::SnapshotReadSucceeded;
	};

	// 标识 presenter 热路径中的真实 CPU 提交调用。
	enum class PresentSubmissionKind : uint32_t
	{
		Present1,
		UpdateLayeredWindowIndirect
	};

	// 每个实际活动帧只复制标量；render/present 时间均为 CPU 侧 QPC。
	struct DrawingFrameTrace
	{
		uint64_t frameSequence = 0;
		int64_t frameStartQpc = 0;
		int64_t previousFrameIntervalMicroseconds = 0;
		int64_t latestSnapshotQpc = 0;
		int64_t latestInputAgeMicroseconds = -1;
		int64_t renderBeginQpc = 0;
		int64_t renderEndQpc = 0;
		int64_t presentBeginQpc = 0;
		int64_t presentEndQpc = 0;
		RECT dirty = {};
		uint64_t latestSnapshotSequence = 0;
		uint32_t contactCount = 0;
		bool hasPhysicalContact = false;
		bool terminalContact = false;
		bool dirtyValid = false;
		bool geometryEmpty = true;
		bool geometryChanged = false;
		bool renderRequested = false;
		bool renderExecuted = false;
		bool presentAttempted = false;
		bool presentSucceeded = false;
		bool forceFullPresent = false;
		bool fullFrame = false;
		bool strokeContent = false;
		bool cursorOnly = false;
		bool cursorDirty = false;
	};

	// 关联 latest snapshot、Modeler 输出与同帧 render/present；前三帧会另存到固定 contact 槽。
	struct DrawingContactFrameTrace
	{
		uint64_t frameSequence = 0;
		uint32_t contactFrameIndex = 0;
		int64_t recordQpc = 0;
		int64_t frameStartQpc = 0;
		int64_t snapshotReadQpc = 0;
		int64_t frameStartSnapshotQpc = 0;
		int64_t snapshotQpc = 0;
		int64_t modelerOutputQpc = 0;
		int64_t downQpc = 0;
		int64_t inputAgeAtFrameStartMicroseconds = -1;
		int64_t inputAgeAtSnapshotReadMicroseconds = -1;
		int64_t previousFrameIntervalMicroseconds = 0;
		int64_t renderDurationMicroseconds = 0;
		int64_t presentCallDurationMicroseconds = 0;
		int64_t renderBeginQpc = 0;
		int64_t presentBeginQpc = 0;
		uint32_t tabletContextId = 0;
		uint32_t contactId = 0;
		uint32_t deviceType = 0;
		uint32_t phase = 0;
		uint64_t contactGeneration = 0;
		uint64_t frameStartSnapshotSequence = 0;
		uint64_t snapshotSequence = 0;
		float x = 0.0f;
		float y = 0.0f;
		float pressure = -1.0f;
		uint32_t modeledPointCount = 0;
		uint32_t realPointCount = 0;
		uint32_t predictedPointCount = 0;
		uint32_t l0PointCount = 0;
		uint32_t l1CommittedIndex = 0;
		float predictionEndpointX = 0.0f;
		float predictionEndpointY = 0.0f;
		bool hasPredictionEndpoint = false;
		bool drawableGeometry = false;
		bool geometryChanged = false;
		bool modelerUpdated = false;
		bool terminal = false;
		bool rendered = false;
		bool presented = false;
	};

	struct PresentTrace
	{
		uint64_t frameSequence = 0;
		int64_t beginQpc = 0;
		int64_t endQpc = 0;
		int64_t previousBeginIntervalMicroseconds = 0;
		int64_t previousEndIntervalMicroseconds = 0;
		HRESULT result = E_NOTIMPL;
		UINT syncInterval = 0;
		UINT flags = 0;
		UINT dirtyRectCount = 0;
		RECT dirty = {};
		PresentSubmissionKind kind = PresentSubmissionKind::Present1;
		bool presentFull = false;
	};

	struct DrawingWaitTrace
	{
		uint64_t frameSequence = 0;
		int64_t waitBeginQpc = 0;
		int64_t waitEndQpc = 0;
		int64_t frameStartQpc = 0;
		int64_t targetDeadlineQpc = 0;
		int64_t previousTargetDeadlineQpc = 0;
		int64_t requestedBudgetMicroseconds = 0;
		int64_t actualWaitMicroseconds = 0;
		int64_t overshootMicroseconds = 0;
		bool returnedBeforeDeadline = false;
		bool deadlineReached = false;
	};

	struct CompositionCommitTrace
	{
		int64_t beginQpc = 0;
		int64_t endQpc = 0;
		HRESULT result = E_NOTIMPL;
	};

	// 两个输入模块可重复设置同一状态；只有 false -> true 会清空一次 trace。
	void ConfigureRtsTrace(bool enabled) noexcept;
	bool IsInputDebugTraceEnabled() noexcept;
	// 在非 RTS 路径创建唯一日志，并写入可识别 build/session 的 BEGIN 标记。
	bool BeginInputDebugSession(const wchar_t* requestedPath = nullptr) noexcept;
	const wchar_t* CurrentInputDebugLogPath() noexcept;
	// RTS 停止后 flush 并写入 END 标记；可重复调用。
	void EndInputDebugSession() noexcept;
	void LogRtsInitializationState(const RtsInitializationTrace& state) noexcept;
	void RecordRtsCallback(const RtsCallbackTrace& callback) noexcept;
	void RecordDrawingInput(const DrawingInputTrace& input) noexcept;
	void RecordDrawingFrame(const DrawingFrameTrace& frame) noexcept;
	void RecordDrawingContactFrame(const DrawingContactFrameTrace& contact) noexcept;
	void RecordPresentSubmission(const PresentTrace& present) noexcept;
	void RecordDrawingWait(const DrawingWaitTrace& wait) noexcept;
	void RecordCompositionCommit(const CompositionCommitTrace& commit) noexcept;
	// RTS 回调停止后统一格式化并输出固定容量的 callback trace。
	void FlushRtsCallbackTrace() noexcept;
#endif

	// 返回当前高精度计时器对应的毫秒时间。
	double GetQpcTimeMilliseconds();
	// 按目标帧率执行睡眠与短时忙等待。
	void HighPrecisionWait(double frameTimeSpentMs, double targetFPS);
	// Debug 限频输出墨迹建模和绘制耗时；Release 中为空操作。
	void LogFrameTiming(size_t committedIndex, size_t realPointCount, size_t predictedPointCount,
		size_t l0PointCount, double workMs, double previousFrameMs, bool idleFrozen);
	// 输出 HRESULT 失败信息。
	void LogHResult(const char* step, HRESULT result);
	// 输出 Win32 错误码。
	void LogWin32Error(const char* step, DWORD error);
	// 将 Win32 BOOL 转换为日志文本。
	const char* BoolText(BOOL value);
	// 将宽字符串转换为 UTF-8 日志文本。
	std::string WideToUtf8(const WCHAR* text);
	// 输出 DWM 合成和颜色状态。
	void LogDwmModeDiagnostics(const char* modeName, HWND window, const char* stage);
	// 查询并输出 DWM 颜色状态。
	bool LogDwmColorizationState(const char* modeName, const char* stage, BOOL* outOpaqueBlend = nullptr);
	// 输出交换链创建参数。
	void LogSwapChainDescription(const char* modeName, const char* stage, const DXGI_SWAP_CHAIN_DESC1& description);
	// 输出已创建交换链的运行时参数。
	void LogSwapChainRuntimeDescription(const char* modeName, IDXGISwapChain1* swapChain, const char* stage);
	// 输出当前 DXGI 适配器和驱动信息。
	void LogAdapterDiagnostics(IDXGIAdapter* adapter);
}
