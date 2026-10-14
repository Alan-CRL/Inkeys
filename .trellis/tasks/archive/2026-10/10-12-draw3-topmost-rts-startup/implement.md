# 执行计划（2026-10-12 已获用户批准）

1. 加载 PRD/research 和 native-desktop Window/Draw3/构建规范；核对原任务后的干净产品差异。
2. 在 IdtMain.cpp 中新增私有前置准备 helper，复用同步 Window Service 根刷新和已有角色句柄，验证实际 owner/置顶状态；关键原因加中文注释。保持编码/换行。
3. 在首次 StartProduct 之前及 ULW 重建后的第二次 StartProduct 之前调用；分别处理准备失败，禁止首次失败误进 DComp 回退。保留现有 failed-cleanup 和 supervision 次序。
4. 检查正常启动、直接 ULW、DComp→ULW、Mag 可用/缺失、刷新失败、属性/owner 不符；确认 Preview observer/milestone 和 whiteboard NOTOPMOST 不变。
5. 复用已有 window_tests 对 root/owner/topmost 的断言。如需新增测试，只扩展能实际断言“失败阻止 Host/每代准备发生于 RTS 前”的现有生产启动 fixture，不为测试引入宽泛公共 mock，也不新增仅匹配源码字符串的测试。
6. 使用已装 Visual Studio 的 ARM64 原生 MSBuild 构建 InkeysRepo.sln Debug | ARM64。同一 PowerShell 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue；设置 MSBUILDDISABLENODEREUSE=1；构建至少允许 5 分钟，超时保留阶段，不重复盲目重启。
7. 构建成功后运行已有 InkeysHeadlessTests --no-window，确认顺序/失败分支静态证据；该命令跳过 HWND 测试，不能冒充实际置顶验证。隐藏 HWND fixture 若要运行须明确确认其无可见界面及当前授权范围；可见主窗口/真实 RTS/PPT 场景必须另获用户明确 GUI 授权。
8. trellis-check 独立审查 minimal diff、初始化次序与清理边界；补充规范，记录结果。用户未授权本任务 commit/push；不得沿用上一任务的提交授权。

## Real-machine validation deferred

在用户允许 GUI 后，以普通可被覆盖的应用作为竞争窗口，验证启动绘制、选择态辅助表面、PPT 进入退出和白板 NOTOPMOST。对首次与重建 HWND 分别记录 root/owner/visible/topmost/Z 序、前置完成、RtsReady、first frame。对 API 成功但实际状态不符保留证据；若顺序恢复仍复现，按 snapshot 定位，不擅自扩大为逐窗置顶或 RTS 重构。
