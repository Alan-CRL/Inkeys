# Trellis Check Report

独立审查范围：本任务四个产品源码文件、两份规范增补及任务 PRD/design/implement/research。已读取 check.jsonl、保存的 hook context、native/native-desktop 两层 index 和最终产品笔锋场景合同。源码/构建由实现代理持有时保持只读；本 reviewer 没有另改产品代码，仅记录审查结论。

## Findings (fixed)

- File: Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp — RunPenTaperProductionProbe terminal assembly.
  Issue: 成功 resume 已解除 EndpointAdmission，最后真实 kUp 未重新 BeginEndpointAdmission 时，AppendEndpointBoundedModeledPoints 因 !active 提前返回；原 endpointTime 也仍使用旧 stopTime，因此 resume 后 Up 输出没有实际进入终态接纳路径。
  Fix: 实现代理按独立审查反馈，在 kUp 后调用 BeginEndpointAdmission，并传当前 lastMovementInputTime。新增 scratch 非空、接纳/可见 pin、精确终点、处理延迟后的 completed/Stored 逐点半径一致断言。最终生产 probe 通过。产品 controller 原终态入口已有正确组装，无需改动。

- File: Inkeys/Inkeys/Drawing/Draw3/Draw3.StrokeGeometry.cpp — RunPenTaperProductionProbe prediction calls.
  Issue: 首轮 probe 在 Disabled predictor 下仍要求 Predict 成功，引发 876 个测试装配失败；固定模型头文件明确说明 prediction disabled 时返回 FailedPrecondition，产品 controller 本来会跳过这些调用。
  Fix: 实现代理给 moving/tracking/resume 三处调用加入与产品一致的 Disabled guard；每次先清 predictedResults，再 RebuildPredictedPoints。最终 331063 个断言零失败，未改变产品预测策略。

- File: Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp; Draw3.StrokeGeometry.cpp — transient taper cap reset.
  Issue: 真实位置变化可具有相同 QPC，单靠 liveTipMovementTime 与 lastMovementInputTime 比较不足以解除旧停笔长度封顶。
  Fix: 实现代理在实际 positionMoved 分支清 liveTipLength，成功 ClearPenTerminalState 也清缓存；Reset 清除全部三项临时状态。已复核 ClearPenTerminalState 只由 Reset/成功 reconnect 等清理入口调用，不在 Lock/完成/Stored 路径清掉应保留的缓存。same-time reconnect fixture 与 36 组真实模型 resume 通过。

## Findings (not fixed)

没有未修复的本任务源码或设计问题。下列既有环境/范围事项未修改：

- 最终 build 的 vcpkg applocal post-build 输出 pwsh.exe 未识别、退出 9009；MSBuild 总退出 0、0 errors，主产品和无窗口测试运行通过。属于本机命令环境及既有构建步骤，不修改工程/依赖配置来隐藏它。
- 最终增量 build 的 7 个 warning 均在未改动的固定 ink_stroke_modeler params.h/internal/utils.h，未扩大到第三方修复。
- 未执行 HWND/Host/RTS、完整 controller owner frame、GPU 像素/D3D Debug Layer、真实设备手感或 Win7 运行验证；用户限定无 GUI，截图现场效果保留人工验收。

## Verification

- Lint / static: PASS — git diff --check --ignore-submodules=all；最终四个产品文件和两份规范的原 BOM 与纯 CRLF 均保留，无无关源码/构建配置/第三方/demo/GPU/持久字段改动。这里的 lint 证据是 diff/格式与静态路径审查，没有声称执行独立 clang-tidy。
- TypeCheck / compile: PASS — ARM64 原生 MSBuild 经 vswhere 定位，完整 InkeysRepo.sln Debug|ARM64；同次 PowerShell 规范化 PATH、禁用节点复用。首次 sandbox 构建环境超时未做源码绕过；完整 retry exit 0 (2:37.06)，最终源码增量仍构建完整 solution，exit 0 (7.85s), 7 warnings / 0 errors。首轮日志确认四个 Draw3 shaders 编译成功。
- Warning scope audit: PASS — 首个成功完整 build 的 371 warnings 全量按诊断位置与最终 zero-context diff 逐项对照，修改行范围内没有诊断；涉及已修改文件的告警仅为未编辑的 IdtMain.cpp:2589、DrawingController.cpp:5581/5586/5588/5712，其余位于未改动业务文件、测试、Windows SDK 或第三方 headers。未声称曾执行额外 baseline build 或 warning 数量相等。
- Tests: PASS — Inkeys.exe --draw3-pen-taper-test exit 0；72 model cases、36 resume cases、331063 assertions / 0 failures；max settle 166.667ms，max outputs 19。多余参数在正常启动前退出 2。
- Existing tests: PASS — InkeysHeadlessTests.exe --no-window 最终 sandbox 外运行 exit 0。初轮 sandbox 的临时配置写入受限仅在环境层解决，没有改变测试/产品算法，也没有开启窗口测试。
- Independent path review: PASS — firstTipIndex 仍从可见末端 endTime−T 按原方式选择（包括 prediction），L0 截取和 L1 protected window/commit cursor 未改；参考 D 来自未收锋 max radius，弧长分布和压缩插值只作用于派生几何；旧/新 dirty union 保持。完成/Stored 共用 solver；未来 live cache 不污染较早物理 Up。
- Model loop review: PASS — 十秒循环实际推进 display 时间并在尚未收敛时调用真实 kMove；LatestModeledTip 的位置与速度独立判定收敛，输出预算、最终宽度与三帧冻结均断言；resume 实际执行 recovery/new-modeled/prediction/geometry helpers。此结论只适用于 CPU 模型/几何路径，不等价于完整 controller 或可见渲染验收。

## Evidence

- research/implementation.md and research/verification.md contain implementation ownership and command exit evidence.
- Build/ARM64/Debug/pen-taper-msbuild-retry.log
- Build/ARM64/Debug/pen-taper-msbuild-verified.log
- Build/ARM64/Debug/pen-taper-probe-verified.log
- Build/ARM64/Debug/pen-taper-headless-verified.log

No commit/push or GUI was performed.
