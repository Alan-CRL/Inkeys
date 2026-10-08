# Bar 渲染帧绘图状态快照

## 修改范围

- `BarRenderLoopState` 初始化时通过 `GetStateModeSnapshot()` 取得一次值快照，初始化笔宽、笔型、Laser 预览与 Fine Dial 量程均使用该值。
- `WakeAndSnapshot` 每帧只获取一次新快照，并由它派生 `BarRenderFrameSnapshot` 的模式、笔型和两种颜色。
- `SubmitTargetsAndLayout`、`AdvanceAnimationsAndDeriveLayout`、`CalculateDirtyAndDrawPresent` 内原 `stateMode` 名称现在是 `state.stateModeSnapshot` 的局部只读别名；笔宽、颜色、透明度与 Layout 粗细辅助函数均走传入快照的重载。
- 保留本文件原有、尚未提交的 Fine Dial candidate 单次读取及延后 `thicknessFineDialLastPenMode` 更新。

## 验证与边界

- `git diff --check -- Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp` 通过；静态脚本确认 72 处 `stateMode.` 读取均落在四个值快照别名作用域（含 `WakeAndSnapshot`），状态依赖的 33 次 getter/Layout 调用均显式传入同一快照，没有无参旧调用。
- 该文件原为**无 BOM UTF-8、CRLF**，修改后仍无 BOM、无裸 LF；不能为了指令中的“BOM+CRLF”措辞加入原本不存在的 BOM。
- 本单元未运行 MSBuild、benchmark 或 GUI；完整 Solution 构建和独立 review 由主任务串行执行。`Bar.Layout.cppm` 的快照重载由主 agent 独占修改，本单元依赖其签名保持一致。
- 状态写入者、Bar 其他文件、窗口副作用和跨帧 UI 自身状态不由此文件保证；本单元仅保证本渲染帧读取的 `StateModeClass` 代次一致。
