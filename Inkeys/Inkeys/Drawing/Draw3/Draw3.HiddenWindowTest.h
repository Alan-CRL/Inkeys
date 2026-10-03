#pragma once

namespace Inkeys::Drawing::Draw3
{
	// 仅创建不可见 Window Service HWND，返回值可直接作为进程退出码。
	int RunHiddenWindowIntegrationTest(bool eraserOnly = false) noexcept;
	// caller 先创建新私有绝对 root；仅真实不可见 ULW Host 生命周期小 smoke，不作性能结论。
	int RunHiddenWindowRuntimeMetricsSmoke(const wchar_t* privateOutputRoot) noexcept;
	// 仅不可见 HWND，比较真实 WARP 清屏、正式 Cursor 与关闭混合的像素读回。
	int RunRendererPixelTest() noexcept;
	// 不创建 HWND，使用真实 WARP context 验证动态 SRV 上传的兼容映射。
	int RunRendererMapCompatibilityTest() noexcept;
	// 无 HWND WARP 故障注入，验证 L1 游标与重试资格。
	int RunRendererFailureCommitTest() noexcept;
	// 无 HWND WARP 故障注入，验证 Laser 双层烘干的失败事务与像素重试。
	int RunLaserRasterFailureTest() noexcept;
}
