# 问题 1 修复版与保留追踪说明

本轮已根据两段实机日志修复实际抓点、模式交接和吸收快照，完成完整 ARM64 构建、无窗口测试与独立审查。Debug 追踪仍自动启用，等待用户人工复测后再关闭问题 1。

## 运行与复现

1. 使用本轮构建的 `D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/Inkeys.exe`。若旧实例仍在运行，先退出旧实例，确保实际使用新构建。
2. 追踪已由 Debug 工程定义 `INKEYS_BAR_BOTTOM_DOCK_TRACE` 自动启用，不需要改设置，也不会额外打开诊断控制台。
3. 第一组：按住主按钮，慢速进入底栏，保持不松手并等待回弹结束，再做一次轻微横移。观察底端位置以及鼠标与被抓点的关系。
4. 第二组：同一手势快速进入、退出、再次进入，回弹未结束时反向；再经过居中捕获区。最后停住片刻，再松手；也可在恢复尚未结束时再次抓取并立即松手。
5. 松手后等待约 2 秒，让已有恢复帧的 1 秒尾段和后台写入完成。若方便，保留一小段录屏以对照发生闪动的位置。

## 日志位置

目录是所运行程序旁边的 `log` 文件夹。对于上面的构建路径，即：

`D:/Project/Inkeys/Repo/Inkeys-draw/Build/ARM64/Debug/log/`

文件名形如：

`bar-bottom-dock-trace-20260909T120000Z-p1234-r987654321-0.jsonl`

同次运行的 run id 相同，末尾数字为分段号。请提供此次测试对应的所有分段 JSONL；超过容量时会保留最近 4 个文件，每个不超过 8 MiB。旧片段可能因上限被删除，因此复现后及时保留该次文件。

日志会记录 dropped、writer_dropped、probe_misses、retention_removed 和 writer_errors。某些抓取基础记录不可用时会明确标记，分析时不会使用默认 80 DIP 或零坐标猜测实际抓点。

## 后续如何分析

主要按成功帧关联输入、形变和最终窗口元组，查看：

- 基准底边、名义可见底边与 dockLine 的差；捕获弹簧运行期间的底边差不直接认定为错误。
- 真正被抓住的局部点经过变形后的屏幕位置：candidate先剔除快照后的窗口追赶再对照原始输入，effective对照生产求解器的有效目标，latest对照上屏时最近输入。solver_constrained明确标出边界/正高度保护。
- grab_solver.raw_screen_y记录帧消费的原始Y，q0仍由独立成功图像probe重建。新模型spring.grip是相对恢复零点的抓点偏移；spring.capture在Docked是目标底线偏移，在Floating是形状高度差，不再套用旧模型的浮动公式。
- SetWindowPos、ULW、作废候选、初值重建和松手吸收前后的同源状态。

可由主会话直接处理回传日志。仓库内也提供离线工具：

```powershell
python -X utf8 .trellis/tasks/08-23-ui3-bar-drag-jitter-collapse-damage/research/analyze_bar_bottom_dock_trace.py Build/ARM64/Debug/log --out Build/ui3-bottom-dock-validation/analysis
```

输出 `summary.md`、`summary.json`、`frames.csv` 和 `absorption.json`。名义几何诊断量不是实际像素读回，不能仅凭某一列数值直接断定闪动根因；如公式记录正常，继续用帧号和录屏核对真实输出。

## 临时改动的退出条件

本轮修复已通过自动验证，当前继续保留全部追踪与原始日志。相同场景通过用户人工复测后再移除临时追踪接线、实现和 Debug 工程定义。Release 排除本临时记录入口。

本次交付程序SHA256：`44f31dc1a7dec0621805b739bb7fc12e1468044c64ea81751677517ea66664f8`。
