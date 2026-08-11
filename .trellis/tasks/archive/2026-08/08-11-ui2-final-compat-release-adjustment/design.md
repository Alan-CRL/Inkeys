# 技术设计

## 1. 现状与根因

### 1.1 UI3 触摸瞬移

`Bar.Interaction.cpp` 的 `WM_TOUCH` 分支已经把活动触点转换为客户区坐标，并合成为带专用 marker 的 `WM_LBUTTONDOWN / WM_MOUSEMOVE / WM_LBUTTONUP`。主按钮命中后调用的 `BarUISetClass::Seek()` 没有使用该消息坐标，而是从头到尾轮询 `GetCursorPos()`。

启动后第一次触摸时，系统鼠标位置可能仍停留在旧位置；触摸兼容鼠标状态随后发生同步，连续两次 `GetCursorPos()` 之间会出现巨大差值。该差值被加到主按钮目标坐标，最后由 `maxX/maxY` 限制到右下角，因此表现为稳定的“瞬移到右下角”。

UI2 的 `SeekBar()` 同样读取系统光标，但它接收的是普通鼠标兼容路径，并通过按下点偏移计算窗口绝对位置。UI3 使用独立 `WM_TOUCH` 合成路径，不能原样复用 UI2 的轮询代码；可复用的原则是一次手势只能使用一个坐标来源。

### 1.2 Draw2 与 UI3 已有边界

`IdtDrawpad.cpp` 的 `CanvasDrawingActivityGuard` 只在 `useInkeys3UI` 为真时调用 `NotifyCanvasDrawingStarted/Ended()`。UI3 侧用原子活动计数把并发笔迹合并为首个开始事件，再投递到 Bar 窗口线程。该入口目前只用于关闭动态光源，适合追加次级面板收起策略，且无需让 Draw2 直接操作 Bar 的 D2D 或动画对象。

## 2. 触摸拖动设计

在 `BarUISetClass::Seek(const ExMessage&)` 内按初始消息来源分支：

- 触摸：
  - 以初始合成消息的客户区 `x/y` 为基准；
  - 使用现有 `WaitForBarInteractionMessage()` 读取本手势后续的合成移动/抬起消息；
  - 仅用相邻触摸消息的客户区增量除以 `barStyle.zoom` 更新主按钮；
  - 收到抬起、触摸取消或退出信号后结束；
  - 不调用 `GetCursorPos()`，也不消费触摸兼容鼠标坐标。
- 鼠标/笔兼容鼠标：保留当前 `GetCursorPos()` 轮询路径。

两条路径共用以下逻辑，避免行为分叉：

- 移动距离累计与 20 px 点击/拖动阈值；
- 主按钮边界夹紧；
- `sustainFlag` 生命周期；
- 松手后的 `PositionUpdate()` 与方向变化刷新；
- 退出时的安全清理。

UI3 不再让隐藏的 `moveRecover` 值折叠主栏；UI2 的 `IdtFloating.cpp` 保持原逻辑。

## 3. 设置显隐矩阵

| 设置项 | UI2 | UI3 | 持久化策略 |
|---|---:|---:|---|
| 主题 | 显示 | 隐藏（现状已满足） | 不改值 |
| 语言 | 显示 | 隐藏（现状已满足） | 不改值；UI3 启动仍使用既有中文约束 |
| 画笔/橡皮/拖动/点击时收起主栏 | 显示 | 隐藏 | 不改值，UI3 不读取其行为 |
| 置顶间隔 | 显示 | 显示 | 共用既有值 |
| 右键主图标关闭 | 显示 | 显示 | 共用既有值 |
| 主栏 UI 缩放 | 隐藏 | 显示 | 继续使用新配置 |
| 启用边缘光影 | 隐藏 | 常规 > 外观 | 继续使用新配置 |
| 启用 UI3 | 实验选项始终显示 | 实验选项始终显示 | 需要重启的旧配置总开关 |
| 动态边缘光影 | 隐藏 | 实验选项；总光影开启时显示 | 隐藏不改值 |
| UI3 调试/动画/动画速度 | 隐藏 | 实验选项 | 隐藏不改值 |

`Setting.cpp` 只调整条件块和容器高度：

- “常规 > 外观”在 UI3 分支追加“启用边缘光影”；
- “常规 > 行为”的四项收起卡整体包在 `!useInkeys3UI` 下；
- “实验选项”移除总光影卡，仅保留 UI3 条件块中的动态光影、调试和动画项；
- 所有隐藏分支都不执行赋值或 `WriteSetting()/config.Write()`。

## 4. Draw2 落笔时关闭 UI3 次级面板

复用 `BarCanvasDrawingActivityMessage` 的窗口线程处理：

1. 通过现有活动计数确认这是有效的首个开始通知；
2. 在任何依赖 `GetCursorPos()` 的动态光源判断之前关闭次级界面，避免触摸落笔时系统鼠标不同步导致提前返回；
3. 将 `drawAttribute`、`geometryAttribute`、`moreExpanded` 设为 `false`；
4. 调用现有关闭函数清理笔型菜单、粗细 Slider、颜色选择器和提示浮层，包括可能的捕获状态；
5. 仅触发 UI3 渲染更新，不改变 `barState.fold`；
6. 再执行原有第三光源休眠逻辑。

Draw2 不新增对 Bar 内部状态的依赖；UI2 因通知 guard 为 false，仍只走 `target_status` 与旧收起配置。

## 5. 一秒平均 FPS 与方向布局

### 5.1 统计

在 `Inkeys.UI.Bar.FramePacing` 中增加可测试的滚动 FPS 统计器：

- 使用 `steady_clock`/显式时间点记录帧间隔；
- 保存覆盖最近约一秒的间隔样本及总时长；
- 每帧加入新样本并移除窗口外样本；
- 返回 `样本数 / 样本总秒数`；冷启动不足一秒时使用已有有效样本平均；
- 非有限、非正间隔不进入统计，避免 `NaN/Inf`；
- 调试关闭后无需强制持续呈现，重新开启时允许统计器重新建立窗口。

`PaceFrame()` 每帧更新显示字符串，替换当前 `1000 / 单帧耗时`。

### 5.2 布局

使用本帧快照中的主栏方向：

- 主按钮位于左半屏、主栏向右展开：文字区域向右延伸并左对齐；
- 主按钮位于右半屏、主栏向左展开：文字区域向左延伸并右对齐。

两种布局都继续把完整文字矩形合并到 `state.current`，确保调试文字和红色脏区边框逐帧更新。

## 6. 版本与兼容性

只更新 `IdtMain.cpp` 中的两处产品版本字符串：

- `editionVersion = L"3.0.0-dev.3981"`；
- `editionDate = L"3.0.0-20260811a"`。

不修改 PptCOM 的程序集版本或历史 Win32 资源版本，因为它们不是本次用户指定的 Inkeys 发布标识。

## 7. UI2/UI3 - Draw2 协调检查

### 7.1 静态路径

- 启动：`useInkeys3UI=false` 只启动 `floating_main`；为真只启动 `Bar::Initialization`。
- Draw2：两种 UI 共用 `stateMode`、绘制队列与撤销记录，不分叉绘制结果。
- UI2：`BrushRecover/RubberRecover/moveRecover/clickRecover` 的读取与写入保持原样。
- UI3：只通过已有状态切换接口和绘制活动通知更新 Bar；不直接从 Draw2 修改 D2D 资源。
- 组件：旧组件开关仍是双框架并行期的持久化来源，UI3 只做运行时投影。
- PPT：保持现有状态切换、自动接管和 Draw2 共享路径，不改 COM 接口。

### 7.2 验证矩阵

| 路径 | 重点验证 |
|---|---|
| UI2 + 鼠标/触摸/笔 | 主栏点击拖动、四项旧收起设置、绘制/橡皮/几何 |
| UI3 + 鼠标 | 主按钮点击拖动、次级面板、设置显隐 |
| UI3 + 触摸 | 冷启动立即轻触/拖动，不瞬移，抬起结束正常 |
| UI3 + Draw2 | 首次落笔关闭次级面板但不折叠主栏，多笔并发只触发一次 |
| UI2/UI3 + 状态操作 | 选择、穿透、撤销、重做、清空的状态一致性 |
| UI2/UI3 + PPT | 放映进入/退出、翻页与绘制状态不因 UI 分支失配 |

## 8. 自动化与人工验证

- 在 `frame_pacing_tests.cpp` 为滚动 FPS 增加确定性时间点测试：稳定 60 FPS、窗口淘汰、冷启动、异常间隔。
- 完整构建 `InkeysRepo.sln` 的 `Debug|ARM64`，包含 `Inkeys`、`InkeysHeadlessTests` 和 `PptCOM`。
- 运行 ARM64 Debug 的 `InkeysHeadlessTests.exe`。
- 执行 `git diff --check` 并复核只修改任务范围内文件。
- 真实触摸、UI2/UI3 切换和 PPT 放映依赖 GUI/设备，交付时提供人工验收清单；未经额外授权不自动启动 GUI。

## 9. 预计修改文件

- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.FramePacing.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`
- `Inkeys/IdtMain.cpp`
- `InkeysHeadlessTests/frame_pacing_tests.cpp`

除非实现时发现直接阻断兼容的证据，否则不修改 `IdtFloating.cpp`、`IdtDrawpad.cpp`、PptCOM 或工程文件。
