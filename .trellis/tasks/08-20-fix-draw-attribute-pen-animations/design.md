# 绘制属性笔型与激光预览动画设计

## 1. 笔型扩展入口

用 SoftPen、HardPen、Highlighter 三个独立 `BarUiValueClass` 替代单一扩展进度。每帧按当前工具提交三组目标：当前支持笔型为 `1`，其余为 `0`；Laser 时全部为 `0`。布局和绘制遍历所有非零进度，分别读取对应按钮的当前 `x/y/frame`，因此支持旧锚点退场与新锚点入场并存。

交互仍只维护一个当前命中区，直接锚定目标笔型；旧视觉不参与 Hover、Press 或菜单。动画推进、窗口需求和脏区续帧必须扫描全部三项。

## 2. 菜单文案

一级菜单使用固定 `标注线`。二级问号浮窗使用锁存的 `penTypeMenuAnchorMode` 解析标题，避免菜单退场或全局笔型变化时改字。SoftPen 使用 `标注线（粗细固定，暂未支持）`，HardPen/Highlighter 使用既有 `启用标注线（暂不可用）`；标题内容和 DWrite 测量在浮窗不可见时同步更新。

## 3. Laser 预览状态机

状态机包含 `NonLaserStable`、`EnteringCore`、`EnteringShell`、`LaserStable`、`LeavingShell`、`LeavingCore`。独立动画值保存语义芯宽、语义形状 morph、白色混合进度、Laser 外宽和红壳揭示进度。

- EnteringCore：红壳保持隐藏；芯宽、形状和颜色从当前值走向 Laser 白芯端点。
- EnteringShell：白芯保持端点；红壳从芯宽展开到外宽。
- LeavingShell：白芯保持 Laser 端点；红壳从外宽收窄到芯宽。
- LeavingCore：红壳保持隐藏；白芯从当前值走向目标非 Laser 语义。
- 稳态 Laser 粗细变化同时 retarget 芯宽和外宽，红壳保持完全展开。

目标工具在任一阶段改变时只改变下一目标和阶段，不重置动画值。EnteringCore 反向进入 LeavingCore；EnteringShell/LeavingShell 在当前红壳进度上反向；LeavingCore 反向进入 EnteringCore。

## 4. 绘制与兼容

预览先以当前红壳宽度绘制实体红层，再绘制统一语义芯层。语义芯层继续使用现有路径、圆角、渐变和 Slider morph，只把颜色按白色混合进度插值。预览包络取芯宽和当前壳宽最大值，不读取尚未进入当前阶段的逻辑目标宽度。

白芯和红壳的曲线端点不再按各自当前 stroke width 独立计算，而是共用阶段化 outer thickness 作为端点直径。圆角矩形使用 `(endpointDiameter - layerThickness) / 2` 的水平 inset，因此壳进度为零时红层被白芯完全覆盖，展开后两层端点圆心仍一致。Slider morph 将 endpoint diameter、core thickness 和当前 shell thickness 一起连续插值到 track thickness。

任务只增加 Bar 内部状态和供 HeadlessTests 使用的纯阶段 helper；不改变应用公开接口、配置或 Draw3 数据流。回滚仅涉及 Bar 与动画测试文件。

## 5. 验证

HeadlessTests 覆盖阶段顺序、交接、反向、扩展入口交叉淡化和文案解析。构建使用 ARM64 Host MSBuild 对完整 `InkeysRepo.sln` 执行 `Debug|ARM64`，运行测试时加 `--no-window`。
