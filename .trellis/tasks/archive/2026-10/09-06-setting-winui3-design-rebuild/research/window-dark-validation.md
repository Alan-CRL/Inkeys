# 窗口框架与深色主题：集成验收记录

日期：2026-09-07。基线 89770357。用户允许继续测试、使用 Computer Use 并直接实施窗口/深色增量；本轮只暂存，不自动 commit/push。

## 实现结果

- Setting 恢复标准 WS_OVERLAPPEDWINDOW 语义；真实 sizing frame 在客户区外，创建尺寸与 NCCALCSIZE 共用系统 DPI 下的 frame，不重复包含原生标题高度。
- NCHITTEST 先保护整个实际客户区，包括右侧垂直滚动条；只有外侧真实 non-client frame 返回 resize。最大化禁用边缘 resize。
- WM_GETMINMAXINFO 仅限制最小跟踪尺寸，保留 USER32 的最大化尺寸/位置，避免目标副屏尺寸被系统再次补偿；任务栏工作区由标准窗口语义处理。
- 标题栏单独绘制不透明当前主题底色；窗口线程异步接收冻结的 DWM 明暗/颜色，避免透明 caption 露出系统蓝色。
- 标题栏右侧新增独立主题按钮，即时浅/深切换并按原 FIFO 保存 SettingDarkMode。旧配置缺项或非 bool 仍为浅色。主题不会改变 Windows/Bar，不重建字体/纹理/设备。
- 页面、卡片、正文、说明、弹层、Notice、滚动条和首页插画共同读取共享 palette；保留全部页面业务和共享 D3D11/session/epoch 管线。

## 已执行检查

- 第一轮完整 ARM64 host InkeysRepo.sln Debug|ARM64：生产程序通过；测试因误用 ImFluent 内部 GetColorU32 接口编译失败，已改用公开颜色转换 API。
- 修正后完整 Solution 构建通过（0 errors；当次增量为3条原 hashlib++转换警告）；集成 InkeysHeadlessTests.exe --no-window exit 0。
- Scripts/i18n.ps1 check：en-US、zh-TW 均306/306，通过。
- 独立六倍率 CPU 控件回归覆盖真实字形/控件 DrawData、浅→深→浅切换及 atlas/字体/尺寸保持；详见 window-dark-test-results.md。
- 独立代码复查发现并修复了副屏最大化二次补偿风险；详见 window-dark-review.md。
- 多显示器修复后的最终完整 ARM64 Solution 构建 exit 0（37.86秒，0 errors，56条既有hashlib++/图像坐标转换警告）；随后集成 --no-window exit 0，约9.7秒。最终构建日志为 Build/setting-window-dark-build.log。
- JSON/JSONL、编码/BOM/CRLF与 git diff --check 均通过；原始编码基线仅保留本次触及文件。

所有完整构建使用 ARM64 host MSBuild、完整 Solution（含 PptCOM）、/m:1 /nr:false 和900秒进程超时。环境键合并去除 Path/PATH 重复仅限本次构建子进程。

## 实际窗口验证状态

本轮 Computer Use 通过 @oai/sky 枚举与尝试启动独立测试副本；应用正常运行，但未暴露可控制的 Inkeys 窗口。只读 WTS 状态确认桌面锁定，已请求用户解锁并打开测试副本设置。锁定后未发送桌面输入，未绕过认证；未把旧 PrintWindow 图或上阶段11张浅色截图当作新代码的验收证据。

收尾再次确认桌面仍锁定；已核对路径与启动时间后结束本任务启动的 PID 17336，将最终编译的 Inkeys.exe 更新到 Build/SettingVisualReview/app。未重新启动锁屏中的窗口；该测试副本 SHA-256 为 5BB9CC3A64E848A8B100DD2658BD812F71B94A876BF81988B573728E215929A5。下一次使用 Computer Use 需重新枚举，不可复用旧 session.json 的 PID/HWND。

因此以下仍为待验收，而非通过：

- 浅深主题按钮点击、标题栏/边框/DWM 的实际呈现与重新启动后的偏好。
- 最大化/还原、任务栏可见、多显示器跨屏及 Snap。
- 普通态八方向拖动；滚动条 thumb/轨道拖动、点击时窗口 bounds 不变。
- 上阶段剩余的长页页底、插件详情、支持/调试、窄窗 overlay 与页面滚动。

代码和无窗口回归不能证明实际 D3D 呈现、Windows 主题边框或鼠标操作体验。任务保留 in_progress，解锁后继续此验收清单。

## 交付

代码、测试、规范与任务记录在最终检查后统一 git add 暂存；不执行 commit/push。Build 下的可执行文件、编译日志和隔离运行配置不进入暂存。
