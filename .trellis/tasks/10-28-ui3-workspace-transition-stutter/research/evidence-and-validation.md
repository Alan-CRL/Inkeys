# Research: 性能证据与验证边界

- Query：当前有哪些可复用的性能证据，三态转场是否已被验证，后续如何避免测错负载？
- Scope：internal；只读源码、已有任务及本机忽略目录中的历史结果。
- Date：2026-10-28
- 源码基线：`bugfix/mode`，`1a5e9ff24c6b5cc186696e78bcbd90a7a9d512b4`。

## Findings

### 1. 现有入口可以复用，但证明范围不同

| 入口/证据 | 直接可确认内容 | 不能证明的内容 |
| --- | --- | --- |
| `Inkeys/IdtMain.cpp:1447` 的 `--state-mode-production-test` | 在产品窗口启动前运行生产状态协调与 FineDial 回归 | 真实 Office、Draw3 首帧、主栏性能 |
| `Inkeys/IdtMain.cpp:1537` 的 `--bar-eraser-offscreen-test` | 真实公共按钮/renderer 的离屏测试入口 | 实际 ULW/DWM 或人的视觉帧率 |
| `Inkeys/IdtMain.cpp:1541` 的 `--ui3-multitarget-benchmark` | 同一 Scheduler + WARP、多目标实际绘制，替换最终 HWND 提交 | 原生窗口呈现、三态业务准备延迟 |
| `Inkeys/IdtMain.cpp:1544` 的 `--ui3-workspace-centering-test` | 真实主栏 coordinator 的居中、隐藏成员、六方向转场、快速反向与失败重试正确性 | 实际时钟下共享 Scheduler 的帧间隔 |
| `Inkeys/IdtMain.cpp:1550` 的 `--ui3-input-worker-test` | 公共输入 worker、队列与续接合同 | 真实触控驱动、全产品输入延迟 |
| `InkeysHeadlessTests/animation_tests.cpp:1731` 的 `--no-window` | 跳过需要 HWND 的窗口测试 | 无窗口结果不等于完整产品通过 |

`Bar.MultiTarget.Test.cpp:175-209` 在每组起始固定一个 workspace；其 metadata 的 `activity=continuous-standard-press-and-page-content` 对应持续按压和页码变化，不是切换负载。

独立居中测试确实覆盖六向和快速反向（`Bar.MultiTarget.Test.cpp:550-557,1692-1699`），不能说仓库没有转场测试。但 `Step` 直接调用 `mainFrame_`，每次人工增加 16 ms（`1504-1510`），没有真实共享调度等待，也没有共同运行 PageControl/Freeze 的整批性能测量。因此它可作轨迹/像素/命中正确性骨架，不能把它通过当作流畅度证据。现有 `TestResults/ui3-workspace-centering/samples.csv` 也属于该口径。

### 2. 历史数据说明有基础渲染预算压力，不能移作当前转场根因

本机 `TestResults/ui3-shared-runtime/baseline-expanded/manifest.json` 标记 `integration-12`、`nativeGui=false`、`budgetPassed=false`，EXE SHA-256 为 `86B6341D7328D171DA71D83533E7826FCB4B67CBC81B4BC927DA26C600053D40`。数据没有绑定当前 HEAD，不是本轮重测。

从其 `summary.csv` / `aggregate.json` 只读汇总：27 组运行中 23 组未满足旧预算；零掉样，静置额外回调为零；每组预热 120 帧、有效活动提交 600 帧、每倍率三轮。

| 负载 | 倍率 | batch P95 范围 ms | 主栏成功提交间隔 P95 范围 ms |
| --- | --- | --- | --- |
| 主栏单独 | 100% | 13.19–13.48 | 37.04–37.68 |
| PPT 四目标 + 主栏 | 100% | 17.77–18.57 | 36.08–36.68 |
| 白板两目标 + 主栏 | 100% | 18.89–21.37 | 34.46–36.10 |
| PPT 四目标 + 主栏 | 150% | 23.08–30.32 | 34.24–37.29 |
| 白板两目标 + 主栏 | 200% | 23.48–25.17 | 34.04–35.19 |

GetDC 是显著的软件等待阶段，但它可以 flush 前面的 D2D/WARP 工作，不能据该列认定慢在内存复制。各阶段 P95 也不能直接相加。上述成功间隔混有基准请求生产节奏，必须核对 producer 时间戳/调度 deadline 后才能归因到 Scheduler，不能仅凭约 34 ms 声称固定睡眠翻倍。

`candidate-mask-slices-partial/manifest.json` 明确 `accepted=false`、只有 21 组完整数据，原因是没有可靠耗时收益，候选已撤回。`restoration.json` 保留撤回内容及哈希。**不得把减少 mask 分片数的旧候选直接重新实施或宣称已有收益。** `baseline-folded-*` 和 `baseline-invalid-activity` 不能替代展开主栏负载。

### 3. 后续采样与验收口径

- 使用当前 HEAD、固定机器/电源/配置与 EXE 哈希；旧结果只作调查线索。保留默认动画、默认光效和真实主栏可见负载，分别测冷切换与热重复切换。
- 六向每方向三轮，默认每轮 30 次切换；每次保留从请求、目标提交到成功末态及 idle 的样本。100%/150%/200%，主栏单独与真实可见 PageControl/Freeze 组合分别测；快速反向覆盖批次中点前/附近/后，不能只固定第六帧。
- 复用现有 RawCaptureReport/schemaVersion=2；采集 callback/batch、七细阶段、mask hit/miss/create/fallback/slices、bitmap 容量、viewport、full damage、backoff、actual commit stamp。按 direction/transition segment 归档，不跨 idle/epoch/generation/失败边界串联间隔。缺少有效 stamp 或掉样的组不得宣称通过。
- 目标沿用 UI3 既有热态 batch P95≤16.67 ms、P99≤33.4 ms；额外报告活动主栏真实成功提交间隔 P95/P99及>50 ms清单，区分真实软件提交和代理时刻。对冷启动、业务等待、同机噪声单列，不以降低帧率/禁光效/隐藏按钮满足预算。
- 性能与正确性分开：生产协调、动画当前值续接、目标幂等、脏区事务、失败重试和命中正确性必须同时满足；GetDC/ULW/ReleaseDC/EndDraw 完整成功才推进成功帧。
- 原生转场与主观手感需要另行获得 GUI 验证授权后人工验收；本轮不启动 GUI，不把离屏数据外推到真实 Office/DWM。

## Related Specs

- `.trellis/spec/native-desktop/rendering-and-ui.md`：公共按钮、共享调度、统一状态派生、脏区、动画批次、连续字体目标。
- `.trellis/spec/native-desktop/ui3-render-diagnostics.md`：真实成功事务、raw 详细计时默认关闭、共享多目标基准及既有预算。
- `.trellis/spec/native-desktop/build-and-compatibility.md`：Solution/ARM64、本机验证与平台声明分离。

## Caveats / Not Found

本轮只读检查，没有编译、执行基准或 GUI。已存在数据不是当前分支转场重现；未发现可直接证明本次用户现场单一根因的完整请求→提交分段样本。当前 `git status` 默认递归子模块会因 HiMsg gitdir 权限失败，使用只读 `--ignore-submodules` 检查主仓，不修改权限/配置/子模块。
