# 2026-10-12 Clear Undo/Redo 验证记录

## 行为与范围

延续 redo-popup，保留 in_progress，不归档、不 commit/push。仅生产 Draw3.DrawingController.cpp 与既有 Draw3.HiddenWindowTest.cpp：最近 Clear 以前区间逐笔 Undo 到空后停止；Redo 按普通笔画、Clear、后继笔画顺序推进到本会话最新。新笔画提交取消旧前进分支。主栏 UI、3 秒等待、工程和 UInk 格式保持。

回归序列 A1/A2→Clear→B1/B2：Undo B2/B1→Undo Clear→Undo A2/A1→额外 Undo no-op；Redo A1/A2→Redo Clear（空）→Redo B1/B2→额外 Redo no-op。覆盖 Desktop 与 PPT 两种 renderer、跨页保留、PPT durable/dirty park 和新笔画取消。生产 CPU probe 另外覆盖页身份、请求取消/迟到、返回 floor 不复活旧 revision、保留恢复点与新请求 retry。

## 已执行结果

- 旧二进制 baseline --draw3-hidden-test：exit 0，all hidden integration checks；仅作基准，不作为新源码结果。
- InkeysRepo.sln Debug|ARM64：生产源码/新增 CPU probe 编译通过，clear-redo-debug-arm64-20261012-relink.log exit 0，11 warnings、0 errors。
- InkeysHeadlessTests --no-window：clear-redo-headless-20261012.log exit 0，Recall/EraserAttribute failures=0，PASS animation correctness。
- inkStrokeModelerTest.sln Debug|ARM64：clear-redo-native-arm64-20261012.log exit 0，74 warnings、0 errors。
- ARM64/Debug/inkStrokeModelerTestTests.exe：clear-redo-native-tests-20261012.log exit 0，InkHistory/UInk/contact 全通过，ThinGPU 5184 cases failures=0。stderr 为已有故障注入测试的预期 I/O/源变化报告。
- 第一轮新增 hidden：仅 PPT fixture 的 4 处 durable 等待断言失败；Desktop 完整链通过，真实 Redo 步骤未报错。根因是 accepted 同计 Save/Load、正常 NotFound 同计 failed，旧 committed==accepted && failed==0 条件不成立。
- fixture 已按真实服务计数修正，并使用最新 index→ReadUInkFile→生产 strict import 检查新的 canonical A 根，防止旧 A 文件或单靠 worker 计数伪通过。PPT 导出包含独立 EndScreen，导入 expectation 与生产同样允许 EndScreen，按普通页身份核 A 内容。
- 第二轮 hidden：第一种渲染模式完整链 PASS；第二种仅计数等待失败。独立确认 Service diagnostics 跨 Start 累计而 Host NotFound 每次 Start 归零，fixture 在本次启动且第一个 PPT 请求前采基线，五个字段改用本 run 增量；实际文件 oracle 和 deadline 保持。
- 最终 InkeysRepo.sln Debug|ARM64：clear-redo-debug-arm64-20261012-run-counters.log exit 0，0 warnings、0 errors；源码未产生新编译诊断。
- 最终两种渲染模式 hidden：clear-redo-hidden-20261012-run-counters.log/.stderr.log exit 0；Desktop 与 Presentation 的 ClearRedoContinuation 各两次 PASS，all hidden integration checks PASS，无 FAIL。

## 构建环境与验证边界

所有构建沿用已有 solution 与 Debug|ARM64，当前 VS 的 ARM64 原生 MSBuild，经 vswhere 定位；同一 PowerShell invocation 规范化 PATH、关闭 node reuse，构建预算 10 分钟。Codex 沙箱 PDB/IPC 与临时存储写入限制已用相同命令的对照验证，实际构建/测试在沙箱外运行，不改依赖、签名、工具链或源码规避环境。

两次 main build 的源码编译均成功，但既有增量链接缓存触发 LNK1000 IncrBuildImage 内部异常；验证目标在 workspace 内后备份对应 Inkeys.ilk，再用原配置重链成功。后续仅 fixture 修改的重编译预先保留 ILK 备份，避免重复进入已证实失败的增量状态，不修改项目链接选项。

相关日志均在 Build/clear-redo-*-20261012*。native build 产生的 4 个根目录未跟踪 .cso 已按确切文件路径清理，其余 build output 由既有 ignored 目录管理。

不启动产品主窗口/真实 Office，不做可见 GUI 或 Computer Use。隐藏 Host 自动 fixture 与真实可见操作不同。Clear 后继 redo 为会话 CPU 状态，不新增进程重启持久化；拓扑保留按已有页 GUID 匹配，删页再插回不在本轮验证承诺中。

## 用户验收与收尾

用户已确认人工验证通过，并明确授权提交修复、结束任务、归档及提交归档与日志。
