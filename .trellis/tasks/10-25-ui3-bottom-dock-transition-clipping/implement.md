# UI3 底栏吸附切换跳变与提示裁剪修复 — 执行计划

> 2026-10-26 最新状态：审阅后修复及全部自动化已完成，`software=passed`；完整 `--no-window` 退出 0（359.523 秒），full-scope `trellis-check` 已完成。先读 `research/review-fix-validation-2026-10-26.md` 和 `research/break-loop-2026-10-26.md`，真实 GUI 未验证，保持 `in_progress`。下方原审阅/实施记录保留作历史，不再作为当前待办。

> 历史审阅：接管提交 `1ba08698` 的脱离、吸收和容量修复应保留，但吸附位置跳变当时未闭环；`research/review-2026-10-26.md` 记录了该缺口及后续修复依据。

## 1. 建立真实复现与失败回归

- [ ] 复用 `CreateBarOffscreenRenderCallback`、实际交互 worker/`Seek` 和现有 UI3 presentation harness，增加 `--ui3-bottom-dock-transition-test`；只替换最终 ULW sink，保留真实布局、动画、D2D 和成功快照。
- [ ] 逐候选/成功帧记录 serial、`e/c`/速度、dt、translation、提示包络、capacity/target/source/destination/dirty 和提交结果，保留被跳过候选。
- [ ] 在 `InkeysHeadlessTests/bar_bottom_dock_tests.cpp` 增加非相同成功/候选 `e/c`、`dt=0` 与 `dt=1/60`、失败候选、超过 24 DIP、zoom/origin/取整的脱离反例；不再使用 `CheckDetachContinuity` 的人工相同映射前提。
- [ ] 增加吸收往返测试：布局吸收前后，base/visual 端点、中心、indicator/hit 和 source/destination 重建屏幕坐标应保持不变，并覆盖立即再次按下。
- [ ] 在 `InkeysHeadlessTests/window_geometry_tests.cpp` 把预测包络**超出**现有 capacity 的反例加入生产顺序测试；现有 `:368-395` 先人工 Deflate 的用例不能作为该覆盖。
- [ ] 修复前确认这些测试分别失败，并记录普通进入吸附是否仍无法复现；不得把正常捕获已有保护误判为失效。

## 2. 修复脱离首帧交接

- [ ] 在 `Inkeys/Inkeys/UI/Bar/Bar.BottomDock.h` 增加以最后成功快照、候选 base/translation/zoom 为输入的脱离 seed 纯 helper，返回普通与 capture-bottom 的位置/必要速度。
- [ ] 在 `Bar.RenderLoop.cpp:6963-7010` 仅由 `presented BottomDocked -> candidate Floating` 播种；首个成功候选不推进两个弹簧，失败/跳过候选从同一成功像素重新播种。
- [ ] 使用 `preservePresentedOffset` 保留真实初值，不扩大或截断 24 DIP 常规保护，不改变弹簧频率/阻尼和动画开关语义。
- [ ] 验证扣除当前指针正常位移后的顶/底额外残差，覆盖回弹中反向、快速再捕获和不同 DPI；进行回弹手感 A/B 审查。

**回退点：**若出现手感或捕获回归，只回退脱离 seed 与调用，保留失败反例；不重写普通捕获 seed、serial 或 barrier。

## 3. 修复吸收快照一致性

- [ ] 在 `Bar.Main.cppm` 为 `RebaseBottomDockPresentedWindow` 增加独立 layout delta 语义，或新增专用 absorb 入口；真实 `SetWindowPos` 的 `screenDelta` 调用保持不变。
- [ ] 在 `Bar.RenderLoop.cpp:12848-12899` 传入 `translation/zoom`，同步平移两轴绝对 base/visual/grip 端点；scale 和相对 offset 不错误平移。
- [ ] 核对 screen center、indicator/hit bounds 在吸收与真实直移下的不同语义，保持奇偶 serial 和几何锁事务。
- [ ] 验证吸收后立即开始下一手势、完整呈现前吸附切换和命中往返，不通过移动 HWND 补偿。

**回退点：**该变更可独立撤回；若影响直接拖动，区分布局吸收和真实直移调用，不叠加屏幕补偿。

## 4. 统一提示/viewport/capacity 并最终兜底

- [ ] 在 `Bar.RenderLoop.cpp` 将无副作用的完整 predicted envelope / candidate viewport 求解排到最终 capacity 决策之前，capacity required bounds 使用同一完整范围。
- [ ] 保留 `ResolveBarBottomDockIndicatorVisualEnvelope`、首次 reveal damage、现有稳定锚点/高水位和 idle viewport 收缩，不每帧缩窗或无限扩大窗口。
- [ ] 在 `Bar.RenderLoop.cpp:9376-9389` 以最终 capacityOrigin 重算 source，并在 `:12306-12327` 前验证 source+size 位于实际 target bitmap；活跃提示同时验证 envelope 包含于 candidateViewport。
- [ ] 越界时禁止钳 source 后提交，保留 full dirty 并有界扩容/重算或 Retry；失败不得推进成功快照。映射变化继续整窗替换。
- [ ] 分别检查 predicted envelope、presentDirty、D2D clip 和 ULW client dirty。只有证据显示 base-only 分支漏标提示 key 时才改 dirty；只有文字上缘异常时才增加 glyph overhang 测量。

**回退点：**容量顺序与最终检查作为独立变更；若性能退化，先检查是否误每帧重建/全脏，不用永久高水位掩盖。

## 5. 逐帧判因与条件分支

- [ ] 运行进入/退出吸附、普通/居中、水平 40 DIP 带、横纵同一样本、释放后立即再拖和提示双向切换，比较异常前后至少三个成功帧。
- [ ] 若普通进入吸附仍出现旧位图 dock 帧，记录新的 barrier/SetWindowPos 序列后再修；不直接套 9 月旧根因。
- [ ] 只有证据指向真实局部抓点代理时，才单独设计归一化抓点/实际仿射修复并重新审查；不得原样恢复历史回退补丁。
- [ ] 软件几何全部正确但屏幕仍异常时，保留真实 GetWindowRect、ULW 参数和屏幕裁剪证据，转真实 ULW/DWM 排查。

## 验证命令

后续实现新增场景入口后的标准验证：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64
./Build/ARM64/Debug/Inkeys.exe --ui3-bottom-dock-transition-test
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

完整 Solution 构建至少给予 5 分钟。`--ui3-bottom-dock-transition-test` 是拟新增的 Debug 离屏入口，当前源码尚不存在，不是本轮已执行命令。必须与居中子任务的真实场景入口及父任务组合矩阵一起运行。

## 真实视觉验收（需另行授权 GUI）

- 从上方/下方进入吸附并分别向上/向下退出；普通底栏和居中底栏；中心左右进入/离开 40 DIP 带及横纵同时切换。
- 回弹未结束反向、释放后立即再拖、失败/viewport 改变后连续切换；检查是否仍有一帧 dock 终态再恢复。
- “底栏模式”与“底栏模式 · 居中”首次 Back 峰值、双向文案切换和淡出；同步记录红/绿 damage、蓝色 HWND、真实 GetWindowRect 与屏幕裁剪。
- 动画开关、既有 DPI/zoom、非零 monitor 原点、底部/侧边/自动隐藏任务栏及适用 PPT/白板场景；记录实际环境，不外推未运行配置。

GUI 未执行或用户原始序列仍复现失败时，不得把静态反例修复或 headless 全绿宣称为本症状已完成。

## 最终集成与审查

- [ ] 与居中子任务串行修改共享文件，运行父任务动态居中 + 吸附 + 提示组合场景。
- [ ] 检查普通捕获、水平远端 seed、release handoff、光源/命中、dirty 和失败恢复无回归。
- [ ] 更新规范中的脱离首帧、吸收重基准和最终 source/target 包含合同；规范与最终代码一致。
- [ ] 通过子任务质量检查和父任务全范围检查，真实视觉未验证项明确保留。

## 2026-10-26 实际执行状态

- [x] 接力差异、所有权、DPI/范围与原始证据复核。
- [x] 本任务软件实现、能够先红后绿的回归及共享文件串行集成。
- [x] 当前 Debug|ARM64 Solution、完整 no-window、两个真实离屏入口及共用隐藏锚点的既有像素回归。
- [x] 全范围 trellis-check、CSV/编码/换行与最小变更范围复核、规范和上下文更新。
- [ ] 授权后真实 GUI/Office/WPS/ULW/DWM、多屏、白板最小化恢复与主观手感验收。

原规划清单保留历史；本节及父任务最终验证报告表示当前执行状态。未收到 GUI 授权，不 commit/push/archive。
