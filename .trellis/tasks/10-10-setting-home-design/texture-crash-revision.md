# 2026-10-11 原生 DX11 纹理生命周期崩溃修复

## 直接授权及证据
用户报告当前版本打开后严重运行时错误，要求修复；继续现有任务、工作区和分支。截图中 ImGui_ImplDX11_DestroyTexture 对空 pTextureView 调用 Release，BackendUserData 非空、TexID=0、pTexture 为空、状态已被标为 OK；输出有 D3D11 Removing Device。源码确认 CreateTexture2D/CreateShaderResourceView 未检查 HRESULT，失败后仍发布 OK 和半成品 backend 数据。设备先被移除的具体触发原因尚不能由截图确定，必须与已确认的资源生命周期缺陷区分。

## 修复边界
保留已批准的书写/300ms停留/归位、Show/Hide及尺寸门禁行为。本轮允许为修复截图中的真实缺陷，最小修改项目定制 imgui_impl_dx11.cpp/.h 和设置调用边界；不升级 ImGui/SDK，不扩展到无关第三方清理。不启动 GUI、Computer Use，不 commit/push，不结束任务。

## 需求及验收
- 纹理创建两阶段都检查实际 HRESULT；仅完整 texture+SRV 成功后发布 TexID/BackendUserData/OK，失败必须释放本次半成品并保持可重试状态。
- 清理幂等、可处理空或部分资源，不空指针 Release；纹理更新/DrawData 提交遇到失败时不能继续使用缺失或失效的纹理。
- 设备失效通过已有共享 RenderPipeline DeviceLost/epoch 恢复路径传播；普通资源创建失败走有界 Retry，并保存可诊断的 HRESULT，不能只吞掉错误或跳过崩溃断言。
- 增加真实无窗口 WARP/生产 DX11 后端回归，覆盖创建失败、销毁、重复清理及恢复重建；如不能无损诱发真实设备移除，明确 fault injection 与实机验证边界。
- 执行完整 InkeysRepo.sln Debug|ARM64（动态定位原生 MSBuild、同 invocation PATH workaround、至少5分钟）和 --no-window；已有主页/WARP/交互回归、i18n/编码BOM/CRLF/git diff --check 保留。

## 实施与审查
1. trellis-research 核查截图/后端/设置与共享设备恢复及可能诱因，记录 research/texture-runtime-crash.md。
2. trellis-implement 实施最小失败事务和调用边界、真实后端回归；主会话统一构建测试，避免并行改源码和编译。
3. trellis-check 独立核查失败资源不被发布、清理幂等、恢复路由和测试有效性；主会话更新必要规范和证据。

## 本地运行日志补充（只读）
最新文件 Build/ARM64/Debug/log/idt1791715266583.log（18:41）记录：首次可见 Settings 帧后，Bar 提交 GetDC=0x887A0005、EndDraw=0x8899000C，新 epoch Bar 资源重建失败为0x8007000E。能确认共享设备移除及恢复期分配失败，不能确认最初设备移除由字标、字体、其它客户端还是系统资源压力引发；禁止据此顺带改写渲染架构或未经测量缩小字体图集。
