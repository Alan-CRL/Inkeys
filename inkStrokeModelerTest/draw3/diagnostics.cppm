module;

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <cstddef>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <string>
#include <windows.h>

export module draw3.diagnostics;

export namespace draw3
{
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
