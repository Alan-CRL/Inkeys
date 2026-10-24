#pragma once

#include <windows.h>

#include <array>
#include <cstdint>

namespace Inkeys::UI::ScrollFollowDebug
{
	[[nodiscard]] HWND Handle() noexcept;
	[[nodiscard]] WNDPROC WindowProc() noexcept;
	// 只由显式启动入口调用；关闭后不安装任何自动重开路径。
	[[nodiscard]] bool Open(HICON icon = nullptr, const RECT* initialBounds = nullptr) noexcept;
	void Destroyed() noexcept;
	// 普通关窗只取消；主线程有序退出时在全局资源销毁前归并导出线程。
	void ShutdownExports() noexcept;
	// 只发布当前导出 STA 实际观察到的选择器顶层 HWND；Mag 线程不访问 COM。
	[[nodiscard]] HWND ExportPickerHandle() noexcept;

	// 只读观察已绘制的真实界面，不能注入帧、修改冻结状态或伪造 owner 回执。
	struct Probe
	{
		bool open = false, frozen = false, exportBusy = false, hasFrame = false;
		bool points = true, flow = true, grid = true, bounds = true;
		int arrowScale = 1;
		double zoom = 1, panX = 0, panY = 0;
		std::uint64_t generation = 0, businessSession = 0, captureSession = 0, sequence = 0, referenceSequence = 0;
		std::uint64_t refreshes = 0, consumed = 0, paints = 0, ownerAppliedSequence = 0;
		double previewHz = 0, displayAgeMs = 0, copyMs = 0, totalTrackerMs = 0, viewportX = 0, viewportY = 0;
		RECT previewRect{}, imageRect{}, freezeButton{}, exportButton{}, resetButton{}, armRecordingButton{};
		unsigned recordingState = 0;
		std::uint64_t recordedFrames = 0;
		// 顺序：Freeze、Points、Flow、Blocks、Bounds、1x、2x、4x、Reset、Export、Arm。
		std::array<RECT, 11> buttons{};
		COLORREF previewBackground = RGB(22, 28, 38);
		POINT paintSamplePoint{}; // 顶部文字裁剪区外的普通留白，可核验真实窗口可见。
		COLORREF paintSampleColor = RGB(28, 34, 46);
	};
	[[nodiscard]] Probe ReadProbe() noexcept;

#ifdef INKEYS_MESSAGE_BOX_TESTING
	// 共享 PNG/JSON writer 的纯文件短测试；不创建窗口或目录选择器。
	int RunExportTests() noexcept;
#endif
}
