# UI3 centered bar dynamic centering — implementation validation

- Date: 2026-10-25
- Task: `.trellis/tasks/10-25-ui3-centered-bar-dynamic-centering`
- Scope: 仅居中子任务；未修改吸附子任务、spec、父任务文档或 `09-06-setting-winui3-design-rebuild`。
- 验证边界：未启动 GUI，未运行 Office/WPS，未声称真实 ULW/DWM 光学呈现已验证。

## 1. 测试先行与旧实现失败证据

先只加入 `--ui3-workspace-centering-test`、真实 `CreateBarOffscreenRenderCallback` 离屏 fixture、成功 serial 采样和 CSV，再运行旧生产门禁。场景使用受控 `frameTime`（16 ms 步进，idle 恢复首帧为 0），通过 `PublishPptSession` / `SetWhiteboardActive` 和 Draw↔Selection 改变真实主栏宽度。

旧实现命令：

```bash
./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
```

结果：退出码 1，`right-100` 共 7 个场景失败。关键成功呈现帧证据：

| 场景 | 成功样本数 | 最大联合中心误差 | 主栏宽度变化 | Y 变化 | 判定 |
| --- | ---: | ---: | ---: | ---: | --- |
| Desktop→PPT | 44 | 105.300000000 px | 219.989053 DIP | 35 px | FAIL |
| Desktop→Whiteboard | 44 | 20.793600000 px | 75 DIP | 35 px | FAIL |
| PPT→Whiteboard | 44 | 20.793600000 px | 75 DIP | 0 px | FAIL |
| Whiteboard→PPT | 44 | 101.275680000 px（控制台值） | 221.850249 DIP | 0 px | FAIL |
| Rapid PPT→Whiteboard | 6 | 23.437500000 px | 38.578125 DIP | 9.990400 px | FAIL |
| Rapid Whiteboard→PPT | 6 | 15.983424872 px | 34.505070 DIP | 14.929600 px | FAIL |
| LeavePpt | 44 | 0 | 219.989053 DIP | 35 px | PASS |
| LeaveWhiteboard | 44 | 0 | 221.850249 DIP | 35 px | PASS |
| FinalLeavePpt | 44 | 0 | 219.989053 DIP | 35 px | PASS |
| RapidFinalLeave | 26 | 0 | 1.423522 DIP | 0 px | PASS |

- 失败发生在中间成功帧，不是终态比较；Desktop→PPT 的重复 Enter 同样为 105.3 px。
- 旧场景 `mappingFailures=0`，因此红色不是 source/target 断言或 fixture 映射失败造成。
- Leave 独立记录并保持通过，没有套用 Enter/Switch 根因或重写 Leave。
- 原始样本：`TestResults/ui3-workspace-centering/baseline-before-fix.csv`。

## 2. 实施的窄修复

- `ShouldDeriveBarBottomDockCenteredRoot` 新增显式 `workspacePlacementActive` 输入；仅在原有 BottomDocked + Centered + Stable + Expanded、无拖动/弹簧完整条件下，将门禁细化为 `!displayTransitionActive || workspacePlacementActive`。
- 普通 display transition 仍为负向所有权；Free、Floating、folded、Capturing、拖动及弹簧门禁不变。
- 实际根反推和 viewport 根范围预测都调用同一个纯门禁，并都传入现有跨帧 `whiteboardDockAnimationActive`；未使用会在 Submit 内清除的 pending 标志。
- 未改变 `displayCenterY`、主栏 `x/w` 动画目标或曲线，未修改 Leave、吸附 serial、弹簧参数或 compensation/rebase。
- 新场景入口在 harness 关闭时仅保留返回 2 的 stub；命令仍在产品窗口初始化前早退，Release 不编译场景 runner。

## 3. 修复后真实 RenderLoop 离屏结果

命令：

```bash
./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test
```

结果：退出码 0，`summary failures=0 result=PASS`。

矩阵共 6 组、72 个阶段报告：

- `right-100`、`left-100`：96 DPI，配置倍率 1，zoom 1。
- `right-150`、`left-150`：144 DPI，配置倍率 1，zoom 1.5。
- `negative-right-100`、`negative-left-100`：96 DPI，monitor bounds 从 `(-1920,-240)` 开始，中心 X 为 -960。
- 每组均覆盖 Desktop→PPT、Desktop→Whiteboard、PPT↔Whiteboard、独立 Leave、最终 Leave 和快速反向。
- 控制台中所有阶段 `maxCenterError=0`、`intermediateError=0`（输出精度为 9 位小数）；CSV 全量 2370 个成功样本的最大绝对误差为 `1.13687e-13 px`，远低于 `1e-6` 判定阈值。宽度行程保持非零，Enter/Leave 的 Y 行程保持非零。
- 每组 Desktop→PPT 注入一次最终 present sink 失败：成功 serial 不前进，后续恢复必须为整窗替换；该阶段成功样本为 43，普通完整阶段为 44。
- 每个成功样本均检查 `pptSrc + psize` 位于真实 target、局部 dirty 位于提交 source 尺寸内、根 X 改变必须 full replacement；所有场景 `mappingFailures=0`。
- 修复后样本：`TestResults/ui3-workspace-centering/after-fix.csv`。

左展 fixture 不直接写 coordinator 的 `displayCenter`：先由正常居中右展建立成功基线，再在 Free 下使用既有显示环境钳制移动根，恢复原显示器后通过 `RequestBottomDockCenterAfterExpand` 和真实 RenderLoop 建立左展居中。此前直接人工跳变产生负 source 的路线已废弃，未用于通过结论。

## 4. Headless 与构建

### 相关回归

临时在现有 `animation_tests.cpp` 入口添加单项选择器，构建并运行后立即还原选择器；最终差异不包含该入口。

```bash
./Build/ARM64/Debug/InkeysHeadlessTests.exe --bar-bottom-dock-only
```

- 退出码 0；包含新增 workspace ownership 矩阵、普通 display transition 负向用例、Free/Floating/folded/拖动/弹簧约束、连续宽度、左右展开、描边、负 monitor center、无效几何和 root range。

```bash
./Build/ARM64/Debug/InkeysHeadlessTests.exe --ui3-centering-related-only
```

- 退出码 0；临时选择器依次调用 `RunBarDisplayTransitionTests`、`RunPptSessionTests`、`RunBarBottomDockTests`、`RunDirtyRegionTests`、`RunWindowGeometryTests`。
- 两个临时选择器均已还原，不能作为当前二进制的永久命令使用。

### 完整 `--no-window`（未完成）

```bash
./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
```

- 第一次运行达到 300 秒工具上限；日志曾捕获新增 helper 将合法的最小宽度 0 误判为无效。该测试向量已改为真正的负宽度，并由上述单项测试确认通过。
- 修正后第二次运行（输出重定向到 `TestResults/ui3-workspace-centering/headless-no-window.log`）同样达到 300 秒工具上限，未完成全套。
- 因此不得将完整 `--no-window` 记录为通过；需要主会话在允许更长运行时间的执行器中重跑。

### 完整 Solution 构建

多轮均使用指定原生 ARM64 host 和 `-p:`，最终命令为：

```bash
"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -v:minimal
```

- 最终完整 `InkeysRepo.sln` 的 `Debug|ARM64` 构建成功。
- 若干重编译日志仍有仓库既有 `C4267/C4244/C4201` 警告（第三方 hash/HiMsg 及既有 Rendering/Setting/IdtMain 行）；未声称 0 warning。
- `git diff --check` 通过；修改文件保持原 CRLF，原带 UTF-8 BOM 的文件已保留 BOM。

## 5. 仍需主会话处理

1. 用更长执行期限完整重跑 `InkeysHeadlessTests.exe --no-window`，记录真实退出码和末尾 `PASS animation correctness`。
2. 当前离屏 harness 不创建 Window Service 窗口组；Whiteboard 最小化/恢复没有无 HWND 的生产状态入口，本轮未覆盖，未为此新增生产旁路。
3. 真实 GUI/Office/WPS、ULW/DWM 光学像素、真实多显示器跨屏/原屏消失、普通居中拖动和吸附附近的视觉/命中验收仍需授权后执行。合成负原点和 96/144 DPI 不等价于真实显示矩阵。
4. 父任务仍需做与吸附子任务的组合集成审查；本子任务未实施或修改吸附修复。

## 6. Full-scope 质量检查与收尾（trellis-check）

### 审查发现与最小自修复

- 生产门禁保持原有 BottomDocked + Centered + Stable + Expanded、无拖动/弹簧完整条件，仅把 `displayTransitionActive` 细化为 `!displayTransitionActive || workspacePlacementActive`。实际根反推与 viewport 都传入同一个 `whiteboardDockAnimationActive`；Free、普通 display transition 和既有 Leave 路径未改写。
- 原离屏 fixture 在左展准备阶段允许最多 8 次非法 source candidate，可能绕过 `pptSrc + psize` 断言。已删除该例外；任何非法 ULW 字段或 source/target 映射都直接失败。收紧后左展和负原点左展均实际通过，没有发生非法 source。
- 原 sink 对非注入的 malformed ULW 拒绝可能只表现为一次未成功帧而被后续成功样本掩盖；现在与其他非法 tuple 一样明确计失败。
- 每个几何阶段现在必须有中间成功帧；每个统计样本必须仍为 Expanded + BottomDocked + Centered + Stable，避免只靠终态或错误形态通过。
- 根 X 的 full-replacement 检查改为从前后成功快照还原布局 DIP 后比较，不再只比较屏幕 X，避免显示器原点、直移或缩放变化掩盖布局根移动。
- 先红后绿证据可信但有边界：旧 `baseline-before-fix.csv` 是同一生产 RenderLoop/离屏链的 `right-100` 12 阶段、396 个成功样本，Enter/Switch 中间帧最大误差 105.3 px；最终 CSV 为 6 组、72 阶段、2370 个样本，最大误差 `1.13687e-13 px`。旧基线未包含后来加入的 present 失败注入，因此不宣称基线与最终 fixture 逐字节相同；核心旧门禁复现与修复后对应阶段一致，Leave 在旧实现中已独立通过。

### 本轮实际验证

- `git diff --check`：通过。最终 tracked diff 仍只有既定 6 个产品/测试/harness 文件；临时测试入口无残留差异，原 BOM/CRLF 保持。
- `./Build/ARM64/Debug/Inkeys.exe --ui3-workspace-centering-test`：收紧断言后退出 0；6 组、72 阶段全部 PASS。日志为 `TestResults/ui3-workspace-centering/check-offscreen.log`，CSV 为 `samples.csv`。每个 Desktop→PPT 注入阶段仍为 43 个成功样本，普通完整阶段为 44；中间成功帧计数非零，所有 mappingFailures 为 0。
- 独立 CSV 复核：2370 行的 source+size 均在 target 内、局部 dirty 合法、Centered tuple 合法、根 X 变化均 full replacement；错误数 0，最大中心误差 `1.13687e-13 px`。负原点两组和左展三组均实际存在样本，不是 SKIP。
- 相关 headless 使用临时选择器构建并运行，随后立即还原入口：
  - `--bar-bottom-dock-only`：退出 0。
  - `--ui3-centering-related-only`：依次运行 display transition、PPT session、bottom dock、dirty region、window geometry，退出 0。
  - 单独测试工程的实际输出是 `InkeysHeadlessTests/Build/ARM64/Debug/InkeysHeadlessTests.exe`。一次误用根目录旧产物导致默认全量测试而非选择器运行，并在 300 秒超时；该次不计为任何通过或完整 `--no-window` 证据。改用上述新产物后两个选择集均通过，临时入口已还原，最终根目录产物由完整 Solution 重链且不含临时选择器。
- 最终完整构建命令仍为原生 ARM64 host + `-p:`：
  `"C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe" InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64 -v:minimal`
  最终退出 0；本轮一次全量重编译约 35.4 秒，还原临时入口后的最终增量 Solution 构建约 9.9 秒。仅有既有第三方 hash `C4267` 警告，未声称 0 warning。
- 本轮未重复完整 `--no-window`，仍按第 5 节交由主会话用更长期限运行。Release harness 关闭时返回 2 的 stub 和产品窗口初始化前早退只做了静态审查，未另跑 Release。未启动产品 GUI、Office/WPS 或真实 ULW/DWM 验收；第 5 节其余人工验证边界不变。真实显示服务存在时负原点 fixture 会明确 SKIP，不能把这种环境的总结当作负原点已覆盖。
