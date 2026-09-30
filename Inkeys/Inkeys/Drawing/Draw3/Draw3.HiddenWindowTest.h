#pragma once

namespace Inkeys::Drawing::Draw3
{
	// 仅创建不可见 Window Service HWND，返回值可直接作为进程退出码。
	int RunHiddenWindowIntegrationTest(bool eraserOnly = false) noexcept;
	// 不创建 HWND，使用真实 WARP context 验证动态 SRV 上传的兼容映射。
	int RunRendererMapCompatibilityTest() noexcept;
	// 无 HWND WARP 故障注入，验证 L1 游标与重试资格。
	int RunRendererFailureCommitTest() noexcept;
	// 无 HWND WARP 故障注入，验证 Laser 双层烘干的失败事务与像素重试。
	int RunLaserRasterFailureTest() noexcept;
}
