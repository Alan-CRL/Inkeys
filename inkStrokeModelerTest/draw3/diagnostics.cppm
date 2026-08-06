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
		const char* probeName = "none";
		const char* probeValue = "baseline";
	};

	// RTS 热路径只复制定长标量，由诊断模块在 Up 时统一输出。
	struct RtsCallbackTrace
	{
		const char* eventName = nullptr;
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
		bool hasRawPosition = false;
		bool decoded = false;
		bool published = false;
	};

	struct WindowMouseObservationTrace
	{
		int64_t qpc = 0;
		DWORD threadId = 0;
		UINT message = 0;
		WPARAM buttonFlags = 0;
		int x = 0;
		int y = 0;
		bool promoted = false;
	};

	// 两个输入模块可重复设置同一状态；只有 false -> true 会清空一次 trace。
	void ConfigureRtsTrace(bool enabled) noexcept;
	void LogRtsInitializationState(const RtsInitializationTrace& state) noexcept;
	void RecordRtsCallback(const RtsCallbackTrace& callback) noexcept;
	void RecordWindowMouseObservation(const WindowMouseObservationTrace& observation) noexcept;
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
