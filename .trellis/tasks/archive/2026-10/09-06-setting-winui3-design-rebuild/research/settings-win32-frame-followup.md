# 设置 Win32 外框修正（2026-10-24）

用户要求修复当前设置窗口：现代 Windows 获焦时正确显示系统主题边框色，Win7 只保留 Aero 外框，避免原生与自绘标题栏按钮重复。截图为用户提供的 Win7 现象证据；沿用父任务并直接实施，不 GUI/Computer Use，不自动 commit/push、不结束或归档任务。前轮提交 f16a28d1，两张既有 CSO 的原字节保持。

## 研究及范围

- 当前 Setting.cpp 明确将 DWMWA_BORDER_COLOR 设为固定浅/深灰，覆盖系统边框着色。
- 现代 Windows 改用 DWMWA_COLOR_DEFAULT，保留系统激活与主题颜色策略，不另读注册表或伪造强调色；保持现有标题栏、材质和渲染边界。
- Win7 重复原生 caption 的兼容修复由 Window 创建/非客户区策略负责；不能关闭 DWM、去掉 resize frame 或禁止系统菜单来掩盖问题。
- 保留现有自绘标题栏、min/max/close、Alt+Space、八方向 resize、最大化工作区、DPI 与恢复行为。
- API 主来源：[DWMWINDOWATTRIBUTE](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute)、[WTNCA](https://learn.microsoft.com/en-us/windows/win32/controls/wtnca)、[Custom Window Frame Using DWM](https://learn.microsoft.com/en-us/windows/win32/dwm/customframe)。兼容方案证据见 settings-win32-frame-research.md。

## 实施与验证计划

1. 完成可靠的旧系统 caption 隐藏与 Aero frame 几何；现代窗口默认边框色只做局部修改。
2. 对旧/新 frame、合成开关、恢复/最大化、DPI 和 resize/client 命中做既有 headless 回归及独立静态审查。
3. ARM64 原生 MSBuild 完整 InkeysRepo.sln Debug|ARM64，同 invocation 规范化 PATH 并禁用 node reuse；沿用前轮正常主机权限，以免受限环境 PDB 管理器错误。
4. 执行现有 --no-window，不启动真实窗口；编码/BOM/CRLF、既有 CSO 字节和 git diff 检查。
5. 没有本机 Win7 实机观感证据时明确保留边界，不将 Win11 编译/headless 当作 Win7 Aero 实机通过。

## 最终实现

- 现代外框不再固定浅/深灰，而是 DWMWA_COLOR_DEFAULT，由 DWM 处理系统偏好和活动状态。
- Win7/Vista 分支在标准样式隐藏创建时先建立 HWND 系统菜单副本，再仅去掉 WS_CAPTION；保留其他 sizing/sysmenu/min/max 位，复用 FRAMECHANGED 和既有真实尺寸校正，不禁用 DWM/Aero。
- 旧 frame 顶部与其他三边一样保留原 sizing inset；现代薄顶行为不变。
- 显式支持旧窗口的 Alt+Space、标题右键、图标菜单与双击关闭消息；菜单项按还原/最大化/最小化及能力启用，命令仍走原 WM_SYSCOMMAND，关闭继续隐藏设置。菜单 modal 前释放所有 ImGui/几何锁，现代消息路径不变。

## 验证结果

- ARM64 原生 MSBuild 完整 InkeysRepo.sln Debug|ARM64：最终 exit 0；主程序、PptCOM 和两个测试目标均已生成，见 settings-win32-frame-final-build.log。
- 首次编译无源码错误，主程序增量链接发生 LINK LNK1000 / Internal error during IncrBuildImage。原生成缓存 Build/ARM64/Debug/Inkeys.ilk 已保留为 Inkeys.settings-frame-stale.ilk，仅移出该缓存后重跑同一完整 Solution，完整链接 exit 0；未修改源码/工程/工具链配置绕过。该恢复遵循微软 /INCREMENTAL 文档中“缺少 .ilk 时完整链接”的行为。
- 第二次构建里 vcpkg 先尝试 pwsh.exe 时 PATH 不含该程序，但其既有 vcpkg.targets 自动回退系统 PowerShell；构建成功。最终增量构建无此输出。
- 新生成的 InkeysHeadlessTests.exe --no-window 全套 exit 0，见 settings-win32-frame-tests.log；包含新旧 frame 的 round-trip、minimum extent、八方向/client 命中和菜单 normal/max/min/能力状态回归。
- 最后仅把 GUI-only 原生样式断言中的 0 改为 0U 消除本轮新增 C4389，期望值和产品逻辑不变；随后完整 Solution 再构建 exit 0。该真实 HWND 样式/菜单断言本轮未执行，因本轮未获 GUI 验证授权。
- 独立审查无已证实阻断问题，见 settings-win32-frame-review.md；不把图标双击消息分支的存在当成菜单 modal 交互实机通过。
- 原 UTF-8/BOM/CRLF 与 git diff --check 通过；两张既有 CSO 的 SHA-256 与本轮基线一致。未改工程配置、依赖版本、主栏/PPT或页面内容。
- 明确边界：当前 Windows 11 ARM64 的构建和无窗口结果不能代替 Win7 Aero/非Aero 的实际焦点、显示、Snap、多屏和菜单交互验收；本轮未使用 Computer Use 或启动交互窗口。
- 不 commit/push，不结束/归档父任务，保持 in_progress。

补充构建依据：[MSVC /INCREMENTAL](https://learn.microsoft.com/en-us/cpp/build/reference/incremental-link-incrementally?view=msvc-170)。

## 提交授权（2026-10-24）

用户明确授权提交本轮外框修复及相关规范、任务、研究和审查记录；不 push。既有两张 shader CSO、本地构建/测试日志与临时基线不纳入提交，保留原工作区文件。使用现有 SSH 签名，失败时停止，不绕过；父任务继续 in_progress，不结束或归档。
