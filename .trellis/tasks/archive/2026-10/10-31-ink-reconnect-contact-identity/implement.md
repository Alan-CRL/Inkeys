# 执行计划

- [x] 检查 dev 的成功重连、普通初始化、Up和手势退休路径，选择最小生产共用测试接缝。
- [x] 增加回归并保持旧接管行为；完整 Debug|ARM64 构建，通过现有无窗口入口记录预期失败。
- [x] 同步重连后的手势身份，补中文原因注释；覆盖连续重连、ID复用、多指隔离及非Touch。
- [x] 再次完整 Debug|ARM64 构建，运行目标回归和适当的既有无窗口检查。
- [x] 独立 trellis-check 审查，记录验证与边界，更新输入规范。
- [x] 按此前已批准的修复包计划，构建完整Release|x64并复用既有发布布局生成本地测试包，检查哈希；不远程发布。

## 构建与验证
用 vswhere 定位 ARM64 原生 MSBuild.exe；同一 PowerShell invocation 先移除重复 PATH 并恢复单一 Path，设置 MSBUILDDISABLENODEREUSE=1。执行 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /nr:false；给中大型编译至少5分钟。Codex 沙箱节点通信异常时使用沙箱外同一构建，不改项目配置。运行测试时使用已核实早退的无窗口参数，Start-Process -WindowStyle Hidden -Wait 捕获退出码；TEMP/TMP可指向仓库Build内隔离目录。

## 状态约束
用户只授权提交上一项诊断工作；本项不自动 commit/push，不创建新 worktree。保留原有未跟踪旧任务图片。计划已在此前回复中展示，并获用户开始修复的明确授权。
