# 第六轮主会话验收：DX11 纹理运行时崩溃修复

2026-10-11，继续 fluent 工作区与当前子任务。截图中已确认的空 SRV Release 缺陷及对应失败恢复边界已修复；本轮自动化验收通过。任务保持 in_progress，未归档、未新增 commit/push，既有入场设计保留。

## 缺陷、实施与独立审查

- 原 CreateTexture2D/CreateShaderResourceView 忽略 HRESULT，失败后仍发布 BackendUserData/TexID/OK；截图中的空 texture/SRV 随后被直接 Release。现在完整 texture+SRV 成功才发布，任一步失败撤销半成品并保留可重试请求，销毁可处理空/部分资源与重复清理。
- 后端保存各 ImGui context 的真实创建/提交 HRESULT；纹理上传或 Map 失败立即中止 DrawData。第二次 Map 失败归还已映射 VB，constant-buffer Map 失败经原管线恢复路径退出。设备对象与初始化事务不发布失败 owner/capabilities；保留原两参数 Init，新增结果输出重载保留 factory 失败值。
- Setting 在 Present 前检查后端结果，在清理 lease 前保存资源 HRESULT 与 GetDeviceRemovedReason；真实失效进入已有共享 DeviceLost/epoch 重建，健康设备的普通资源失败进入原有有界 Retry。初始化 coroutine 的 DeviceLost 不再被覆盖为 Retry；日志包含 stage/code/reason/epoch 并去重。
- trellis-research、trellis-implement、trellis-check 已完成；审查修复 Init 无 owner 时丢失真实 HRESULT 的缺口，并复核最终夹具修正后冻结源码。详见 texture-crash-implementation.md、research/texture-crash-check-review.md。
- 规范只同步 rendering-and-ui.md 中相应事务/失败传播与验收合同。trellis-update-spec 技能未安装，沿用已读取 workflow Phase3.3 的手工同步方式；未冒称调用缺失技能。

## 最终执行结果

- 完整 InkeysRepo.sln Debug|ARM64：动态定位 ARM64 原生 MSBuild，同一 PowerShell invocation 应用 PATH workaround，允许1800秒；最终 ExitCode=0，27.2498582秒，0 Error、3 Warning。警告均来自未修改的 hashlib++，新 Map 局部未初始化警告已消除。
- InkeysHeadlessTests.exe --no-window：最终 ExitCode=0，172.6073485秒，stderr=0字节，终行 PASS animation correctness。无交互式 GUI、Computer Use 或用户进程终止。
- 生产 DX11 后端、嵌入 CSO、无 HWND 的真实 Windows11 ARM64 WARP FL11.0：真实零宽描述得到 E_INVALIDARG，失败帧像素保持透明清屏；随后真实 texture 创建、更新、销毁、重新初始化及主页绘制通过。NULL/partial/repeated teardown、Map rollback 均有回归。
- 两个创建阶段的 E_OUTOFMEMORY、DEVICE_REMOVED、DEVICE_RESET 采用明确的一次 HRESULT 故障注入，其余资源/提交/像素为真实 WARP；这不是实际 TDR 或移除真实 WARP device。同一健康 device 的 backend 重建也不是完整 native 共享 epoch 故障实测。
- 原主页真实三语言/浅深/导航展开收起/五种宽高/七种倍率共420布局，输出224播放、196跳过；原许可/仓库/Star/赞助轻点、真实键盘导航、拖动取消及滚动/控件回归通过。入场首帧、Hold、曲线归位、透明边缘、k/cap实像素仍通过。
- Scripts/i18n.ps1 check：en-US、zh-TW 各384/384 PASS；本轮无新增可见文案，不重复 sync 或手改生成物。
- task.py validate：implement/check 各12条，Exit0。大型 rendering-and-ui.md 存在注入截断提示，本轮代理已定向读取实际相关章节，未依赖截断内容。
- 最终编码/UTF-8/BOM/CRLF/末行与 diff 审计：见 ignored encoding-audit-texture-crash.json；HEAD与暂存区保持 a3b22667 基线。

## 首次验证失败的真实记录

首次完整构建 Exit0、458.284119秒；首次 headless Exit3、17.4100798秒，未计为通过。新增夹具假设 WARP 拒绝16385×1，但本机实际接受；之后夹具直接改状态复用而未撤销成功 owner，触发 WantCreate 断言。修正为真实零宽描述，并在复用前始终经生产 WantDestroy/UpdateTexture 清理；没有增加产品尺寸限制。最终重建和完整重测结果以上述成功记录为准。

## 未验证边界与后续现场诊断

截图和本地日志确认 DeviceRemoved 先于最终 AV，恢复期 Bar 新 epoch 曾返回 E_OUTOFMEMORY；最初触发设备移除的原因仍不确定。本轮修复已证实的资源发布/清理缺陷并补齐恢复路由，不能据此宣布原生共享设备的全部潜在触发源已消除。未启动完整 Setting HWND/coroutine 验证，也未运行 Win7 SP1+KB2670838 实机；只静态沿用原 FL11.0 API/管线，不添加专用 shader/device/thread/timer。若现场仍有设备失效，新诊断会保留失败阶段、原 HRESULT、移除原因和 epoch。

## 可复查证据

- Inkeys/Cache/home-design-20261010/build-texture-crash-final.log、.stdout/.stderr、-result.json。
- 同目录 headless-texture-crash-final.stdout/.stderr、-result.json；首次失败保存在 *-texture-crash-verified-* 记录。
- 同目录 encoding-audit-texture-crash.json；research/texture-runtime-crash.md、research/texture-crash-check-review.md。
