# F-039：PPT 冷加载待决时的双向 SlideID 拓扑投影

日期：2026-09-28。本单元只修改 `Draw3.DrawingController.cpp/.cppm` 的生产 materialize 与显式无窗口回归；未改 UInk 文件格式、worker、Host/Office、窗口或设备。`FLIP_SEQUENTIAL` 和仅 DComp→ULW 的首发 presenter 合同不变。

## 实施内容

- `MaterializePresentationSlot` 对 StableSlideId 先核 `target` 的正数、唯一 SlideID，再对同一已加载快照的旧 active 普通页与 retained 页建立统一的 `SlideID → Canvas` 集合。源 page GUID 在 active、retained、EndScreen 之间必须唯一；重复/非正 SlideID、重复 EndScreen、EndScreen 带 SlideID、保留页缺 SlideID/标记身份或把 EndScreen 混作普通页均返回失败，不部分安装。PageIndexFallback 保持原 ordinal/EndScreen 路径。
- 按最新 target 的有序 SlideID 从统一集合选出 active Canvas；原 retained 102 再次出现时恢复同一 page GUID、笔迹和 interval 来源，并从 retained 集合移除。旧 active 102 被最新 target 删除时留在集合，转成 retained，保留原 page GUID/笔迹，补齐既有 `inkeysPageState=retained` 标记后交给 F-031 的同槽 builder。EndScreen 按其独立标记原样保留，不参与普通 SlideID 映射；旧 N 页文件仍按既有行为补独立空 EndScreen。
- 该逻辑只构造 CPU 值槽，仍由 F-038 的活动安装 helper 在 `mutationRevision==0` 门禁后成组安装；旧 completion、新用户写入、workspace key 和 worker durable 状态不在此凭空改写。`MarkPresentationCanvasRetained` 只更新应用 page-state 标记并保留其它 extra；`operations.clear()` 沿用先前 retained materialize 合同。

## 红绿证据

- `RunPendingPresentationTopologyLoadTest() noexcept` 直接调用**生产** `MaterializePresentationSlot` 和 `BuildPresentationSaveRequest`，不创建 HWND/Office 或触碰文件。轨迹 1：旧 active `{101,102}` → 最新 `{101}`，确认 102 以原 page GUID/点成为 retained，再保存仍带正确 marker；轨迹 2：旧 active `{101}` + retained `{102}` → 最新 `{101,102}`，确认 102 以原 GUID/点回到 active 且 retained 不重复。两方向都核独立 EndScreen。负例覆盖 active/retained 重复 SlideID、active 重复 ID、重复 page GUID、非正 ID、双 EndScreen。
- Stage 1 保留旧 materialize，完整 `InkeysRepo.sln Debug|ARM64` Build 退出 0；同一 CLI 退出 1，共 9 项断言失败。原始 stderr 保存在忽略目录 `TestResults/release-hardening/draw3-pending-topology-red-debug-arm64.stderr.log`。Stage 2 改生产投影、**未改测试**；完整 Debug|ARM64 Solution Build 退出 0，同 CLI 退出 0，受影响的 F-038 loaded、F-031 parked、F-029 Desktop 三个无窗口入口也退出 0。绿构建日志为 `draw3-pending-topology-green-build-debug-arm64.log`；CLI 进程退出码由主 agent 的验证记录保存。
- `git diff --check` 通过；Controller 两文件保持原 UTF-8 BOM/CRLF。独立 reviewer 仍需审两向来源核验、EndScreen/旧 schema 与最终 diff；实施者自测不充当独立复审。

## 限制、风险和回退

- 本测试从合法形状的生产值快照进入 materialize，尚未在真实 Office 多文稿、正在异步加载时删除/恢复页、实际 worker UInk 严格读取与磁盘故障中验证。Win7 SP1+仅 KB2670838 的 Hardware FL11.0、无 FL11.0→WARP、ULW/DComp 成功 Present 仍为人工矩阵；本机 Win11 ARM64 构建不能外推。
- 拓扑变化时保留已删除页必然使冷加载 CPU 值集包含更多历史 Canvas；不在活动绘图帧循环增添扫描或 GPU 资源，未单独测大文稿加载尾延迟。已经由旧版本遗漏或污染的用户文件不会被本补丁猜测修复/删除；严格导入失败仍保留最后有效文件并需隔离诊断。
- 若需局部回退，仅撤销 F-039 的统一投影/冲突核验与对应 CLI，保留 F-038 活动安装、F-031 同槽 retained 来源及此前稳定功能；不可用整个 Controller 文件回退其他工作单元。
