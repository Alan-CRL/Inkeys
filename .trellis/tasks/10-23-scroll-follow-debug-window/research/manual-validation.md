# 实屏人工复核步骤（供工具无法覆盖部分及交付使用）

这些是待执行步骤，不能当作已通过。实际结果另记 validation.md。

1. 使用本轮 Debug ARM64 Inkeys.exe，添加 --scroll-follow-debug-window；需要日志时独立添加 --scroll-follow-verbose。不带参数启动应无调试窗口/预览采集。
2. 在专用 Edge 测试页或 Word 内容上启用主栏视图调整。检查 Capture Session/Frame 与 Tracker 输入序号，首帧 initializing -> tracking，图像来自 WorkArea，原区/输入/处理尺寸清楚。
3. 将调试窗口覆盖正文，检查预览中覆盖处仍为真实底层内容而非调试窗口。应同时核导出 raw.png 与正文专用可识别内容，不能只凭 HWND 列表或窗口变小作通过。
4. 正向/反向短滚动；核箭头方向、原始 delta/total、点分类、参考网格和主/竞争支持范围。支持范围不应标为滚动客户区。
5. 缩放窗口、滚轮缩放图像、拖动平移和复位；所有标记和图像仍对齐、比例正确。切换点/箭头/网格/范围开关和箭头倍率，统计数值应不变。
6. 触发拒绝直到 Paused；检查最后失败输入仍在、来源/首失败门限清楚、未执行项显示 N/A/未执行，最后可信 total 不跳变。Capture 失效应单独标识，不冒充 Tracker 拒绝。
7. Freeze 后继续滚动，图像/标记/该帧统计固定，live 头部明确仍活动；解除后直接追上最新。检查 Frozen 与 Stale/Capture Failure 不混淆。
8. Save Debug Snapshot，选择本次专用目录。核 raw.png/preview.png/snapshot.json 身份一致、检查有效性和原数值可读；取消导出不创建输出。默认无自动屏幕保存。
9. 关闭调试窗口，继续视图调整；检查不退出进程、不改变 ViewAdjust/穿透/画布/定格。不自动重开；新进程带参数才能重新打开。
10. 有限性能对照：同类专用内容，关闭/打开/关闭后各 15-20s，记录 Capture FPS、Read/Tracker/复制耗时、显示年龄和内存；记录来源/帧数，不能把 UI 刷新率当 Capture FPS，不能外推任意分辨率与 Word/硬件笔。

仅使用 Computer Use 的正式 node_repl + @oai/sky 控制 UI；所读 guidance/api/confirmations 位于本机 computer-use 技能 docs。不要操控用户其他浏览器标签、账户、安全或锁屏界面。专用测试窗口结束后关闭；如果桌面锁定立即停止视觉动作并报告。程序内自检与无窗口测试不能替代未做的人工视觉检查。

## 已实现的定向入口（均需严格单参）

- `InkeysHeadlessTests.exe --scroll-follow-debug-only`：无窗口 CV 差分、三槽寿命与捕获观测、共享导出 writer；不替代 GUI。
- `Inkeys.exe --scroll-follow-protocol-test`：原 Business 控制链路的无 HWND 合同，不是真实 Mag。
- `InkeysHeadlessTests.exe --exit-visibility-gate-test`：屏幕外窗口线程/生命周期合同。
- `Inkeys.exe --scroll-follow-debug-exclusion-test`：产品完整 GDI 窗口覆盖专用源，两图案各至少三张真实 Mag 帧、覆盖 ROI/关闭/重建检查；不运行旧长尾。
- `Inkeys.exe --scroll-follow-debug-product-test`：无交互性能/生命周期入口。absent先观察15秒，然后真实Off/反向ACK/完全drain；开窗后实际toggle/F0/双窗ACK启动新会话，open/closed各15秒保持新会话，各阶段1秒热身。自动执行正32/反16 tagged wheel、真实关闭、Off/on/off与关闭后不重开。absent与后两段会话不同，仅有限观察、不能推断性能因果。此入口不再等待人工操作，也不应在计时中点击；控件请按前面1–10步用正常启动参数验证。closed是同一纹理最终+16姿态，不声称三段逐像素相同。

后二者受原安全层检查保护；安全界面返回 blocked/exit4 时停止，不能改代码绕过。最小窗口历史版本和完整 UI 都需要各自的真实像素证据；只有后续完整 UI 通过不能追溯标记早先被拦截的尝试。

产品 GUI 子系统 EXE 在 PowerShell 直接 `&` 可能不等待结束；应使用 Start-Process -PassThru 后 WaitForExit/ExitCode，并在仓库根目录运行。不要把外层 PowerShell exit0 当产品 PASS。可用下面形式（每次只运行一个入口，不额外追加 --scroll-follow-debug-window）：

```powershell
$diagnosticProcess = Start-Process -FilePath 'D:\Project\Inkeys\Repo\Inkeys\Build\ARM64\Debug\Inkeys.exe' -ArgumentList '--scroll-follow-debug-exclusion-test' -WorkingDirectory 'D:\Project\Inkeys\Repo\Inkeys' -PassThru
$diagnosticProcess.WaitForExit()
$diagnosticProcess.ExitCode
```

输出保存在已有普通目录 Build/ScrollFollow 下的新 live-PID-tick 或 product-live-PID-tick；测试不使用个人配置/保存路径。product 夹具主屏要求至少1344×720，只约束该夹具，不提高产品系统/屏幕要求。Rejected/Paused 的真实 GUI、Edge/Word 正文和硬件笔仍按前面步骤人工验证。

正常启动只在捕获开始前开窗。不要把测试中的中途动态Open当作正式支持路径：旧动态夹具已有一次实际输入覆盖像素失败（seq113/max_error225），尚未证实具体根因；没有追溯改判。新的夹具用真实Off/drain模拟无在途捕获的启动前置，保留所有像素门。选择器首次可见的动态排除仍需单独实测。当前工具未能枚举专用测试窗口，真实控件操作未完成；用户已选择控件交付人工验证，请正常带启动参数进行第1–10步；短入口没有75秒人工等待。

2026-10-23用户明确选择“控件项目交付人工验证”。原生工具后来枚举出实际诊断窗，但get_window_state被应用授权检查拒绝，返回Computer Use was not approved to use inkeys；没有执行任何控件输入。该许可边界与此前安全桌面门分开，不能改系统权限绕过。

用户确认后新增样式要求：诊断主窗口为TOPMOST，owner仍为空；启动后及画布层级切换后检查置顶状态，关闭不重开。此前非置顶运行记录属于旧版本，TOPMOST实际交互按人工验收执行。
