# S5 P1/P2 独立验证门禁

完整 `InkeysRepo.sln Debug|ARM64`：退出 **0**。动态发现 Visual Studio ARM64 原生 MSBuild，`/m:1 /nr:false`，同 PowerShell invocation 规范化 PATH，900 秒期限；没有修改工程、SDK 或依赖。正常用户构建证据：`TestResults/ui3-workspace-transition/s5/p1p2-verified/build-{command.json,stdout.log,stderr.log,exit.txt}`。

本阶段 EXE SHA-256：`84354A02ECF8EBFD7B939705D4A360C1EC57A9BE78DB43B204279E30FFF6BD77`。冻结 EXE 与采样在同目录。独立复审见 [P1/P2 复审](s5-review-p1p2.md)。

## 正确性

以下均为该哈希的真实进程退出码 0，各自日志与 result.json 保留在上述目录：

| 入口 | 实际覆盖 |
| --- | --- |
| `--page-control-hidden-test` | WindowCandidateNative/PageControlHidden/WhiteboardNative 均 failures=0；真实屏外 pair 第二窗失败与回滚、取消、代际、普通 Show/显隐幂等、显式 promote；暂停真实 owner 时其他共享 Scheduler 客户端推进，完整 ACK 前不发布新命中/页码 ready；ULW 成功而 native Show 失败及恢复；四窗 DPI/倍率、资源/ULW失败回归 |
| 同一 hidden 入口中的 WhiteboardNative | 真实 1920×1080 屏外 Freeze、生产 RenderBackground、D2D/GetDC/ULW/Window Service；prepared 与最终 visible 分开；真实 owner 延迟 Hide 不提前 inactive ACK；主题和设备失效；像素已成功且 native 未决时不重复整屏绘制，测试总 pixels=5 |
| `--state-mode-production-test` | workspace 工具隔离、重复意图、快速切换、旧回调、源槽隔离、shortcut/clear 与 FineDial 版本回归；使用工作区 TEMP |
| `--bar-eraser-offscreen-test` | 公共 renderer/Scene 像素和命中；新固定捕获 serial、旧/重复回执、新 damage 保留；PageControlUI3 failures=0 |
| `--ui3-workspace-centering-test` | 六向、快速反向、左右/负屏幕原点、倍率与几何/失败成功事务；summary failures=0 |
| `--ui3-input-worker-test` | 生产 mailbox/dispatcher/continuation/deadline/cancel |

Whiteboard 生产测试直接串行驱动 RenderBackground；执行前已经注销并 drain 四个 PageControl callbacks，没有并行使用共享图形 owner。它证明此明确软件与屏外窗口边界，不证明实际 Office/WPS、DWM 光学呈现或触控设备。

第一轮 hidden 的唯一失败是普通同步 Show 的 completion 早于 owner 缓存快照发布。已修正成功路径的发布顺序；测试继续等待完整 known/矩形/visible/非 iconic，并断言期间不额外提交像素，未放宽为 desired 或 API 入队成功。

## 同机同口径三轮 A/B

双方均为 Debug ARM64，命令 `INKEYS_PAGE_CONTROL_TRANSITION_PERF=1 Build/ARM64/Debug/Inkeys.exe --page-control-hidden-test`。每轮六方向 × 3 cycle，双方各 54 次完整转场、0 incomplete。性能运行没有与构建或其他测试并行。默认动画、光效、WARP、真实 ULW 和 Window Service 保持。

基线 SHA-256：`EA7B51CAA0423037568A432CA1222D3BC0DA8EDC98B421F5797CAD64FE2DEBFA`。双方原始数据：`s5/{baseline,p1p2-verified}/run-{1,2,3}/{frames.csv,summary.csv,metadata.json}`；三轮退出码均为 0。计算脚本 `s5/analyze.py`，完整结果 `s5/p1p2-ab.json`。

| 指标（全部批次，包括冷首帧） | 基线 | P1/P2 |
| --- | ---: | ---: |
| 四窗 batch 数 | 1,791 | 10,185 |
| batch P50 ms | 11.887 | 1.877 |
| batch P95 ms | 21.027 | 3.128 |
| batch P99 ms | 28.580 | 4.584 |
| 匹配像素及原生回执的成功提交数 | 旧字段未记录 | 19,943 |

同口径 batch P95 下降约 **85.1%**。这是屏外 unlimited 软件工作批次，不是实际显示器 FPS。

新版本的 candidate→receipt：20,087 个原生确认（含隐藏确认），P50/P95/P99 = **0.032/0.110/0.378 ms**，最大 13.656 ms。真实 owner 成功完成的像素提交间隔：19,835 个有效间隔，P50/P95/P99 = **2.022/3.359/4.924 ms**，最大 12.872 ms；方向/Idle 边界不串联。

旧版没有新增的 owner 时刻列，因此不伪造旧成功间隔进行对比。新回执时刻与 callback-end、ULW-only attempt 分开；19,943 个成功提交也不等于 40,740 次 callback。队列峰值与 coalesced/stale 是每轮累计计数，应取最终值/最大值，不对每行累计值求和；逐轮原始值保留在 A/B JSON。

生产 RenderSurface 已不调用同步 SetBounds/Show/Hide，旧 set_bounds_ms 为跨 owner 等待，新列为 0；visibility_ms 现在为候选发布成本，metadata 明确其语义变化。UI 仍保留真实绘制/GetDC/ULW 成本，不声称这些 API 永不等待。

## 下一阶段与限制

P1/P2 必要正确性、独立复审和三轮 A/B 门禁通过，可以推进 P3/P4。全量 `--no-window` 也已退出 **0**，用时约 6 分 42 秒，末尾 `PASS animation correctness`；headless EXE SHA-256 为 `E697CA62FA9B6085D6C4B2CC431B4281A5AB4FDB982F3D0FEE64F7F99CDEEDBE`，日志/真实退出码在 `s5/p1p2-headless/`。既有测试向旧任务路径生成的十张 PNG 已按源码输出路径核对并移入该阶段证据目录，没有修改无关测试。

P3/P4 仍需另行完整构建和回归。真实主产品 Office/WPS、任务栏、物理 DPI/显示器切换及主观观感尚未验收，不由屏外结果外推。任务保持 in_progress，不 commit/push/归档。
