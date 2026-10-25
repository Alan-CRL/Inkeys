# 第六轮：DX11 纹理运行时崩溃实施记录

2026-10-11，继续同一任务/工作区/分支；保留已批准并行书写、300ms停留、归位及 Show/Hide/尺寸门禁实现。本轮不 GUI、Computer Use、commit/push，不归档任务。

## 已确认的缺陷与修复

截图直接崩溃来自 ImGui DX11 的空 pTextureView->Release。源码会在 CreateTexture2D 或 CreateShaderResourceView 失败时仍发布 BackendUserData/TexID/OK。现在两阶段检查实际 HRESULT，失败逆序释放本次部分资源，保留 WantCreate/无 ID/无 backend data；只有完整成功后才发布。销毁始终清理标识，可处理旧版本 NULL/partial 状态及重复 Invalidate。

后端保留 void UpdateTexture/RenderDrawData 公共入口，新增每 ImGui context 的 GetLastResult。上传失败在 DrawData 调用中立即返回，不访问无效 GetTexID，也不继续绘制。VB/IB 创建与 Map 记录实际失败；第二次 Map 失败归还已映射 VB。constant-buffer Map 失败跳过 draw 并恢复已保存管线状态。device objects 的全部创建失败撤销半成品，NewFrame 不再依赖断言继续提交。Init 的 factory 查询与 lease 引用完成后才发布 backend owner/capabilities，防止恢复时空 AddRef。

Setting 在 Present 前检查实际后端结果。初始化、设备/字体重建、呈现建立、提交、Present/遮挡探测/resize 失败均先查询 GetDeviceRemovedReason，再清理；真实设备失效（包括 HUNG/INVALID_CALL 等 negative reason）返回既有 DeviceLost，健康设备普通 OOM/E_INVALIDARG 返回既有有界 Retry。初始化 coroutine 的 DeviceLost 不再被外层覆盖成 Retry。记录 stage、原 HRESULT、deviceReason 和当前 epoch；仅 stage/code/reason/epoch 变化记录一次，成功恢复后重新启用下一次诊断，避免重复每帧日志。

初始 D3D11 Removing Device 的具体原因尚未确定。研究指出品牌几何提交没有新增纹理/设备/shader，原始日志可确认 shared DeviceRemoved 后新 epoch Bar 资源 E_OUTOFMEMORY；不以该现象擅自改字体密度或设备配置。

## 回归及测试边界

现有 FL11.0 无窗口真实 WARP/生产 DX11 后端中新增：
- 先分配8×8 CPU像素，向真实 WARP 提交零宽 texture 描述触发 E_INVALIDARG；保留可重试状态、无部分发布，失败页面提交真实像素全为透明清屏。夹具复用前始终经过生产销毁路径，意外成功也不遗留 owner。
- 仅 Headless 项目宏下，两阶段单次注入 E_OUTOFMEMORY/DEVICE_REMOVED/DEVICE_RESET，确认失败帧中止、原 HRESULT 保留、原 texture 成功重试/动态 block 更新/清理。
- 构造截图中的 NULL SRV/NULL texture 与有 texture 无 SRV 状态，真实 Update 和重复 Invalidate 安全清理，随后重新创建嵌入 shader 与图集。
- 第二次 buffer Map 注入 OOM 后真正 WARP 再次绘制成功，避免残留映射。
- backend Shutdown/re-Init、无 owner 的失败 Init、真实主页/图集再次上传绘制；共用实际 IsSharedDeviceLoss 生产分类 helper 的 HUNG/INVALID_CALL/健康 OOM 回归。

错误注入替换指定 API 的返回值，其余资源与绘制仍为真实 WARP；没有移除真实设备、触发全系统 TDR，不能称为真实移除设备实测。完整 Setting HWND/coroutine GUI 生命周期与 Win7 实机仍是人工项。主会话统一实际 ARM64 Solution build、--no-window、独立 Trellis 审查；此记录不把尚未运行的检查写作通过。

## 文件与静态检查

产品：imgui_impl_dx11.cpp/.h、Setting.cpp、Setting.SessionState.h。
测试：setting_home_warp_tests.cpp、setting_session_state_tests.cpp、Headless vcxproj 单一测试宏。
已核本轮源码 UTF-8 无 BOM、原 CRLF 保持及 git diff --check；没有修改新增文案/生成 i18n。实际编译和全部无窗口测试由主会话冻结后统一执行。

## 首次统一验证及夹具修正

主会话完整 ARM64 Solution 构建成功（Exit0，458.284秒）。首次完整无窗口测试 Exit3（17.410秒）：新增夹具假设16385×1会被拒绝，但当前 WARP实际接受该尺寸；随后夹具直接改状态复用而未销毁已成功创建的 owner，导致断言。此失败属于新增回归夹具，不能作为产品尺寸缺陷证据，也不新增产品最大宽度限制。

夹具已改用真实零宽 D3D 描述；任何意外成功都先通过生产 WantDestroy/UpdateTexture 清理，且打印实际与预期 HRESULT。测试注入控制流相关的 idx_resource 未初始化警告通过原局部结构零初始化消除。以上为主会话复验前修正；此记录尚不宣称修正后完整测试通过。
