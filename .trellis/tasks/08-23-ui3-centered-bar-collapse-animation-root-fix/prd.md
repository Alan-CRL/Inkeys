# UI3 居中底栏主栏收缩动画根节点修复

## Goal

修复居中底栏从 Draw 切换到 Selection 时主栏先向左/左上移动、宽度视觉停滞、随后闪现并跳到居中短栏的问题。稳定居中态不再通过动画补偿维持中心，而是从当前主栏动画几何逐帧反推主按钮根节点。

## Background

- 本任务是 `08-20-ui3-bar-bottom-dock-feedback` 的独立 P1 子任务。录屏 `D:\Personal\Downloads\2026-08-23 12-28-28.mp4` 证明上一轮方向锁存、清理上升沿和两阶段重基准在真实设备上仍保持原故障。
- 先前“遗留换向状态导致逐帧 forceRestart”的解释只能说明一种可能路径，不能解释修复后现象完全不变，因此不得继续作为已确认根因或验收依据。
- 原架构让居中所有权分散在主按钮位置、水平视觉补偿、成功帧重基准、方向锁存和 viewport 门禁之间。状态数量和提交顺序使动画几何、可见中心与消息坐标很难保持同一合同。
- 普通主栏 hover 的 X/Y 必须从同一成功呈现快照逆映射；这一修正对真实水平弹性映射仍然必要，予以保留。

## Requirements

### R1. 稳定居中根节点所有权

- 仅 `BottomDocked + Centered + Stable + Expanded` 且无拖拽、水平弹簧和显示切换时，渲染线程可以反推 `mainButton.x`。
- 反推输入只包含当前动画值下的主按钮、主栏及两者可见描边；绘制/几何属性面板、More、Popup 和提示框不参与中心计算。
- 主栏继续使用 `MainBar.Inherit(Center, MainButton)`，所有按钮和扩展面板继续沿原继承树定位；不得改变既有父子关系。
- 根节点位置使用当前显示器水平中心逐帧求解，主栏 `x/w` 的动画目标、曲线和进度不因居中而重启。

### R2. 其它位置所有权保持

- Center 捕获、拖拽、脱离、恢复、水平弹簧或显示切换期间继续由原状态机和 `displayCenterX` 持有位置。
- Free 底栏、浮动、折叠、白板首次放置和普通非居中换向保持现有逻辑。
- 居中态不再根据动态 HWND 中轴重新分类方向；非居中时窗口宽度无效则保持当前方向。

### R3. 删除旧补偿状态机

- 删除 centered layout correction、补偿平移、pending/in-flight 两阶段 rebase、成功/失败备份回滚及其活跃渲染门禁。
- 删除仅服务于该方案的稳定方向锁存、居中方向锁存、遗留换向清理上升沿和相应测试。
- viewport 预测直接传播由 `mainBar.x/w` 动画范围反推的根节点 X 范围，不再用补偿 outset 扩张整个包络。

### R4. 消息坐标一致

- 普通主栏按钮 hover 使用同一个成功呈现快照完成主体 X/Y 逆映射。
- 不原地修改消息；More 与刚性浮层继续使用原始刚性坐标。
- 触摸指示器的屏幕/布局坐标问题不在本任务范围内。

## Acceptance Criteria

- [ ] Headless 覆盖根节点所有权矩阵、左右展开、描边和无效几何。
- [ ] 模拟 Draw → Selection 的 `mainBar.x/w` 曲线时，宽度单调收缩且每一帧联合可见外框中心不变。
- [ ] 源码中不再存在 centered correction、centered layout rebase 或 stale-side cleanup 路径。
- [ ] 非居中水平弹性映射和 hover 双轴逆映射覆盖继续通过。
- [ ] ARM64-host 的 `InkeysRepo.sln` `Debug | ARM64` 完整构建通过。
- [ ] `InkeysHeadlessTests.exe --no-window` 与 `git diff --check` 通过。
- [ ] 不启动可见 GUI，不创建 commit；真实设备视觉结果由维护者复核后才能宣称故障已修复。

## Out of Scope

- 不重构全局动画类型或修改 `SetTar()` 语义。
- 不修改底栏阈值、弹簧参数、提示框文案或触摸指示器坐标。
- 不改动 UI2/IdtFloating、Draw3、配置项或本地化资源。
