# 共享帧几何合同与验证边界

## 证据等级

本文记录仓库现有规范/构建合同，以及本轮可执行的无窗口验证入口。规范描述的是应遵守的设计约束，不能单独证明当前生产代码已正确实现，也不能代替本次症状的逐帧复现。

## 与两个子任务共享的技术合同

### 1. 水平居中的对象与时机

- 当前规范将水平居中定义为正交于 `Floating / BottomDocked` 的状态，不应把竖向吸附和水平居中合并为互斥枚举：`.trellis/spec/native-desktop/rendering-and-ui.md:871-876`。
- 规范中的居中几何是主按钮、主栏及可见描边的联合外框；扩展面板不进入中心计算：`.trellis/spec/native-desktop/rendering-and-ui.md:929-932`。
- 对稳定、展开、已吸附且居中的状态，规范要求在主栏和按钮动画推进之后、Popup 等下游绝对几何派生之前，从当前联合外框反推 `mainButton.x`，同步 `displayCenterX`，并重新执行 `MainBar.Inherit(Center, MainButton)`。禁止另维护逐帧 correction 或补偿平移：`.trellis/spec/native-desktop/rendering-and-ui.md:942-946`。
- 场景合同规定 Desktop 首次进入 PPT/Whiteboard 才捕获桌面基线；仅已展开的 BottomDocked 主栏贴底 5 DIP，保留 Centered/Free；最终退出恢复基线。PPT↔Whiteboard 不覆盖桌面基线：`.trellis/spec/native-desktop/rendering-and-ui.md:942`、`.trellis/spec/native-desktop/whiteboard.md:23-27`。

本次居中调查必须特别验证：当 workspace 场景、非 Stable 阶段、宽度动画和竖直贴底同时存在时，根位置的唯一所有者是谁，以及上述稳定居中反推是否被跳过。规范中的“稳定态才反推”不能自动解释或豁免用户看到的偏心。

### 2. 吸附首帧连续性

- 两轴 mode/phase/elastic offset/direct translation/display 环境应由同一 `bottomDockTransitionSerial` 事务发布，渲染不得提交只更新一轴的帧：`.trellis/spec/native-desktop/rendering-and-ui.md:929-939`。
- 水平捕获与脱离首帧应从上一成功呈现像素反推恢复初值；水平 40 DIP 捕获带内的有效偏移不能被 24 DIP 普通保护截断：`.trellis/spec/native-desktop/rendering-and-ui.md:938-940`。
- 纵向只允许由最后成功呈现的 `Floating -> BottomDocked` 识别捕获，并从旧屏幕底边与新基准底边反推 capture-bottom 初值。首次成功捕获帧不推进弹簧，失败/跳帧应从同一旧成功像素重新播入；旧底边距 dock 可超过 24 DIP：`.trellis/spec/native-desktop/rendering-and-ui.md:941`。
- D2D 几何完成后到 ULW 前仍需校验 serial、deferred barrier 与实际 direct translation；不一致时应放弃提交并保留完整重试，而不是把新位图与旧窗口位置拼接：`.trellis/spec/native-desktop/rendering-and-ui.md:939`。

这些条目直接对应“首帧跳到任务栏、下一帧恢复”的假设空间，但必须用生产实现和逐帧样本判断究竟是捕获播种、弹簧推进、serial/barrier，还是窗口提交配对失效。

### 3. 提示包络、窗口区域与脏区

- 规范要求提示文案变宽时先扩外框再交换文字，变窄时先交换文字再缩外框；首次激活帧即应把上次/当前外框、Back 峰值、描边、光影、Gaussian 与抗锯齿的完整包络纳入 damage：`.trellis/spec/native-desktop/rendering-and-ui.md:948-949`。
- 每帧顺序应是动画/继承布局 → damage → capacity/viewport → 以同一 `presentDamage` 约束 D2D clip/Clear 与 ULW dirty。`pptDst/psize/pptSrc/prcDirty` 在同次 ULW 调用提交；映射改变须整窗替换：`.trellis/spec/native-desktop/rendering-and-ui.md:720-738`。
- 只有 `GetDC → UpdateLayeredWindowIndirect → ReleaseDC → EndDraw` 全部成功才推进 dirty、viewport、mapping 和命中快照；失败须保留下次全量恢复：`.trellis/spec/native-desktop/rendering-and-ui.md:731-735`。
- 调试开关 `Experimental.Inkeys3.UI3.Debug.Enable` 同时控制 damage 红/绿框与真实 HWND 蓝框，可用来区分业务脏区和窗口外框不足：`.trellis/spec/native-desktop/rendering-and-ui.md:735-737`。

因此裁剪调查需要分别回答：绘制包围盒是否完整、viewport 是否预留完整包络、D2D clip 是否覆盖、ULW source/dirty 是否覆盖、真实 HWND 是否与 candidate viewport 同帧一致。仅扩大某一个 RECT 不能证明全链修复。

## 可复用的不变量

1. **居中不变量**：每个成功呈现帧，以同一帧的主按钮/主栏/可见描边计算的实际屏幕联合中心，等于所在 monitor 水平中心（只允许既有整数像素取整误差）。
2. **屏幕像素连续性**：吸附状态变化不能先把旧内容直移到新吸附目标再于下一帧恢复；首帧偏移由上一成功像素反推，且首张捕获帧不提前积分。
3. **两轴原子性**：同一输入样本导致的横纵模式、弹性状态、窗口位移和显示环境必须来自同一稳定 serial。
4. **呈现原子性**：位图容量原点、source、window size、destination、dirty 与真实 HWND 必须表示同一个 candidate frame；失败不能推进成功快照。
5. **完整包络**：提示的实际绘制/光影范围必须同时被 damage、D2D clip、source/viewport 和真实窗口外框覆盖。

## 验证入口与本轮边界

- 当前仓库存在 `Build/ARM64/Debug/InkeysHeadlessTests.exe` 和 `Build/ARM64/Debug/Inkeys.exe`。
- 本轮基线命令：`./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`。
- 后续实现的标准构建入口是完整 `InkeysRepo.sln`，`Debug | 当前设备原生架构`，使用对应原生架构 host MSBuild，构建超时至少 5 分钟：`.trellis/spec/native-desktop/build-and-compatibility.md:5-24`。
- 无窗口算法、离屏 D2D 或替换最终 HWND sink 的基准只能证明软件几何/事务，不可声称真实 ULW/DWM、任务栏、跨屏、Office/WPS 或视觉动画已验证：`.trellis/spec/native-desktop/ui3-render-diagnostics.md:95-103`、`.trellis/spec/native-desktop/whiteboard.md:60-62`。
- 真实 GUI 验收需另行明确授权。本轮不启动 GUI；最终规划应把真实进入/退出 PPT、白板、吸附边界与调试框逐帧检查列为后续人工验收，不得用 headless 通过替代。
- 若需要新增诊断，优先复用成功呈现事务、现有 raw capture/阶段采样和调试框，不增加默认开启的逐帧磁盘日志：`.trellis/spec/native-desktop/ui3-render-diagnostics.md:30-47`。

## 本轮基线结果

- 已执行 `./Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`，退出码 `0`，耗时约 `339.883 s`，日志末尾为 `PASS animation correctness`。
- 随后以原生 ARM64 host MSBuild 对当前 HEAD 执行完整 `InkeysRepo.sln -p:Configuration=Debug -p:Platform=ARM64`，构建成功，`0 Warning / 0 Error`，耗时约 `3.85 s`；各编译/链接目标均报告 up-to-date，测试二进制无需重链。因此前述测试结果与本次构建确认的是同一产物。
- 在 Git Bash 中应使用 `-p:` 而不是 `/p:`；后者会被转换为参数 `p:` 并触发 `MSB1008 Only one project can be specified`。首轮命令因此失败后已用正确参数重跑成功，不作为产品构建故障。
- 该结果仅说明当前既有无窗口/离屏回归通过；用户症状仍存在，后续必须用能复现这些场景的新增回归或逐帧证据补足覆盖，不能把全绿当作问题不存在。
- 本机构建工具为 `C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\arm64\MSBuild.exe`。本轮没有启动 GUI 或真实 Office/WPS，真实视觉症状仍未复现验证。

## 后续证据记录要求

每个症状至少记录：输入/场景、serial、animation dt、主按钮/主栏当前值与目标值、capture/spring 初值、frame/presented translation、capacity origin、source/size/destination、业务/调试 damage、真实 HWND RECT、ULW/EndDraw 结果。采样应以成功呈现帧为主，同时保留被跳过/失败候选，避免只看最终稳定位置。
