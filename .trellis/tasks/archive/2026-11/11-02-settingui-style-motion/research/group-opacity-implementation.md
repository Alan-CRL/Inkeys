# 整块透明度接入与开屏提速（2026-11-03）

## 实现

- 合成放在 DX11 backend 内（`[Inkeys]` 标注）：新增 `ImGui_ImplDX11_OpacityGroupBegin/End` 哨兵（同 `ImDrawCallback_ResetRenderState` 机制）和 `ImGui_ImplDX11_SupportsOpacityGroups()`。所有调用 `RenderDrawData` 的路径（普通帧、开屏捕获/回放的子 DrawData、测试）自动正确，资源随 `InvalidateDeviceObjects` 释放，不在 Setting.cpp 另造生命周期。
- backend 每次提交先按列表/命令顺序配对哨兵，求组内顶点外接框∩scissor 的整像素矩形与嵌套层级；只有存在哨兵时才准备资源。每层嵌套一张目标尺寸 RGBA8 离屏层（最多4层），仅清零并合成组矩形；全部清除/合成四边形一次 Map 上传。组内保留原 `SRC_ALPHA/INV_SRC_ALPHA` 得到预乘 RGBA；合成复用默认 PS、颜色 (o,o,o,o)、最近点采样与 `ONE/INV_SRC_ALPHA`，不新增着色器/CSO 资源。尺寸变化重建，空闲2s归还纹理；创建/Map 失败写 LastResult，调用方按既有仲裁拒绝 Present；中途失败恢复调用方 RTV。
- UI 侧（Setting.Controls.cpp）：`PageBlockScope::Finish` 先做位移（顶点、child 裁剪、已并入命令的原 child 裁剪），再 `CommitOpacityGroup`：按 ImGui 提交顺序把本块 child 树并入块所在 DrawList 并清空原 child 列表，在真实 IdxOffset 边界插入 Begin、末尾插入 End。条件块与所属块同范围时透明度相乘（不嵌套），嵌套条件块与旧页淡出（`ApplyContentAlpha`→`ApplyGroupOpacity`）形成嵌套组。opacity 1 不插哨兵；0、无合成 backend、分通道时退回逐顶点（0 时等价）。
- 开屏 `HomeEntryTiming`：等待 .24→.14、书写 1.06→.90、停留 .60→.32、归位 .54→.44、落定 .22→.18，总长 2.66→1.98s（约快 26%），主要压缩静止停留与首尾空等；各阶段曲线不变。

## 验证（Debug|ARM64）

- 完整 `InkeysRepo.sln` 构建 exit0，仅既有 warning（build6.log）。
- `INKEYS_FONT_WARP_ONLY=1 --no-window --setting-home-only`（真实 WARP backend）exit0，stderr 空。新增 `CheckOpacityGroups`：浅/深 × 不透明/半透明目标，opacity .1/.2/.5/.9 与“无块背景/完整块”逐通道插值的最大误差均为 2；嵌套 .5×.5 误差 1；同场景强制旧逐顶点路径误差 52–59（证明能检出轨道透过滑块）；不透明帧无哨兵且 child 列表照常提交；成组时嵌套 child 列表已并入；InvalidateDeviceObjects 后重建像素一致。
- `INKEYS_SETTING_MOTION_ONLY=1 INKEYS_SETTING_MOTION_SNAPSHOTS=1 --no-window --setting-home-only` exit0，stderr 空，28 张 WARP 样张 failures=0（中途帧可见深色 toggle 滑块正确覆盖轨道）。
- 开屏测试原硬编码 2.66s 时刻改为 `HomeEntryPhaseTime(legacy)` 按阶段映射；时序常量断言更新为新值。
- `Inkeys.exe --i18n-locales-test`、`--setting-font-lifecycle-test` exit0。
- 完整 `InkeysHeadlessTests.exe --no-window` exit0，404s，stderr 空（full.log，含上述组透明度 WARP 回归）。
- 日志：`Build/settingui-style-motion/group/`。

## 未验证 / 边界

- WARP 只证明合成公式与资源路径；真实 GPU（尤其 Win7 集显）上的帧耗时与开屏观感需人工确认。离屏层为目标尺寸（1080p 约 8MB/层，通常 1 层），仅动画期间存在。
- Win7 连续 resize、Win10/11 最大化材质的人工验收项不变。
