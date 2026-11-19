# 设计：脏区调试帧率显示改造

## 1. 范围与边界

只改主栏宿主（`UI/Bar/`）的调试覆盖层、配置缺省值，以及 UI3 帧节拍模块中的帧率计算。这些改动只在调试开启时生效，调试关闭的路径保持不变，因此 golden 与各项离屏测试应当不受影响。

## 2. 实时帧率（R2）

- UI3 `UI3.FramePacing.cppm` 新增 `InstantFrameRate`（与 `OneSecondFrameRate` 并列，保持 UI3 不依赖产品层）：
  - `Tick(activeFrameTime, now)` 返回 `{ actual, unlimited, valid }`：
    - `actual = 1 / (now − 上次 Tick)`；
    - `unlimited = 1 / activeFrameTime`。
  - 首次 Tick、`Reset` 之后、时钟回拨或间隔非有限时 `valid=false`。
  - `Reset(now)` 与现有恢复路径对应。
- 主栏 `BarRenderLoopState::frameRate` 改用 `InstantFrameRate`：
  - 帧尾采样点（`Bar.RenderLoop.cpp:13124`）每个成功呈现的帧都格式化一次 `state.fps`；`valid=false` 时用 `FrameRateUnavailable`。
  - 休眠恢复时重置（`8037`）保持不变。
- 文字在帧 N 绘制时，显示的是帧 N−1 结束时算出的值，也就是「最近一个已完成帧」。当前帧的耗时在绘制时还未知，这是唯一可行的定义。
- `OneSecondFrameRate` 不再被产品使用。按最小改动原则保留，它的 Headless 测试也保留；是否删除另行决定。

## 3. 位置（R4）

- 锚点用主栏底板 `shapeMap[MainBar]` 的绘制矩形中心（`inhX/inhY` + 宽高，转成 DIP），不读它的透明度：收起时透明度为 0，但几何仍居中在主按钮上。展开/收起动画中锚点随 `x/w` 平滑移动。
- 底栏吸附形变时，主栏底板按 Body 映射绘制。锚点矩形经 `TransformBarBottomDockBodyRect` 换算成像素后取中心，文字与点以这个中心排布，本身不参与形变。
- 排布：
  - 文字：12 DIP，常规字重，水平居中；行高沿用现有的 `max(20, 实测)` DIP。
  - 点：位于文字下方，间隔 2 DIP；10 个直径 4 DIP 的圆，圆心间距 8 DIP，总宽 76 DIP。
  - 「文字 + 点」整体在竖直方向以主栏中心居中。
- 文字和点的包络（像素，四舍五入外扩 1px）替换现在的 `currentDebugTextBounds`，并入 `currentContentBounds`，因此伸出主栏时窗口会相应变宽。去掉原来的翻到上方和按侧对齐逻辑。为避免窗口越界，按现有方式钳到可用宽高以内。

## 4. 颜色与指示点（R3、R5）

- 颜色：`state.debugFrameSleepLatch.IsPending()` 为真（与 `ResolveDebugFrameColor` 判定相同）时用 `RGB(0,255,0)`，否则用 `RGB(255,0,0)`，透明度 1.0。文字与点共用同一个画刷颜色。
- 帧序号：`BarRenderLoopState` 新增 `debugFrameIndicatorSerial`，在成功呈现的提交点（`13029` 附近，与 `frameRateSamplePending` 同处）递增。绘制时隐藏第 `serial % 10` 个点，`serial` 为本帧提交前的值。这样第 1 帧隐藏第 1 个点；失败重试的帧序号不变，隐藏的点也不变。
- 调试开关切换（`8010` 的边界）时序号清零，下次开启从第 1 个点开始。

## 5. 脏区（R6）

- 现有链路不变：开启时 `debugRendering`/真实渲染帧把 `currentDebugTextBounds` 并入 `state.current`（`12431`），`lastPresentedDebugTextBounds` 在下一帧并入擦除（`12302`），关闭时 `debugOverlayRefreshPending` 清理（`8013`）。
- 只需扩大包络（文字 + 点），不新增渲染需求，也就不会空转。
- 文字位置每帧都可能变化（主栏移动或动画），旧包络 ∪ 新包络已经被上述链路覆盖。

## 6. 缺省值（R1）

- 修改 `Other.Config.cppm:600` 中 `ShowFrameRate` 的缺省值为 `false`，以及 `Bar.Main.cppm:72` 的初值为 `false`。
- 设置页从 `Inkeys::config` 读取，不需要改；启动时 `IdtMain.cpp:3369` 的同步也不变。
- 已有 `true` 的配置照旧显示，不做迁移。

## 7. 风险与回滚

- 风险：包络计算错误会留下残影。用现有离屏 `--ui3-attribute-dirty-test` 检查调试关闭的路径（不受影响）；调试开启路径的覆盖关系与现有文字相同，只是包络换了，靠实机确认。
- 回滚：改动集中在 `Bar.RenderLoop.cpp` 的帧率文字段、`UI3.FramePacing.cppm` 新类和两处缺省值，按文件撤回即可。
