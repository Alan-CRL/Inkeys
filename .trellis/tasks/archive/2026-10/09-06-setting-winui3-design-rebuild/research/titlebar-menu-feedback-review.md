# 标题栏版本色与汉堡反馈独立审查（2026-10-09）

当前增量以 `research/approved-titlebar-menu-feedback.md` 为准，仅审查 HEAD 已包含调试任务提交之后的未提交差异。独立读取实际产品/控件/vendor 路径、最终测试、构建与测试日志；本审查没有启动 GUI、Computer Use、commit/push，也没有修改产品源码或测试。

## Findings (fixed)

独立审查没有发现需要新增修复的源码问题。

- `Setting.cpp` 的实际版本 AddText 从 TextSecondary 改为 TextPrimary，与主题、Debug、caption 的主文字语义色一致。同用活动 1.0、非活动 0.55 的透明度系数与 ImGui alpha；版本字号、布局、窄窗让位、命中与路由未改变。
- `Setting.Shell.cpp` 的汉堡保留透明默认填充、透明描边和原 ID/尺寸；局部增加 ControlFillSecondary/Tertiary 到当前 palette subtleHover/subtlePressed，六次 PushStyleColor 与 PopStyleColor(6) 成对。真实 Design::Button → ImFluent::Button → ButtonBehavior 仍承担输入、held/hover、颜色动画及焦点环，释放后仍沿原 state.Toggle 调用一次，不增加路由或业务动作。
- 浅色反馈目标为 hover 231、pressed 224，深色为 53、47，均与各自侧栏底色 243、32 区分。不修改 vendor、全局普通按钮颜色、配置或渲染线程/资源边界。既有源已编译，不需要工程登记、模板更新或国际化新键。
- 审查期间与实施代理核对并修正测试端点要求：vendor AnimateColorU32 使用 83ms 指数渐近和 8 位颜色取整，60Hz 下可停在目标上方 2 LSB；颜色断言使用 3 LSB，同时要求 held 比前一 hover 严格更接近 pressed，避免仅放宽容差造成误通过。释放后等待由 20 帧增加到 30 帧，原 alpha<=3 要求保持。产品动画不变。

## Findings (not fixed)

无未解决的本轮源码/测试问题。最终构建的 9 条 warning 来自既有第三方 HiMsg/hashlib++ 等，未扩大本轮范围清理。

GUI 的实际鼠标观感、DWM 显示及实机多 DPI 仍未执行；本轮使用真实 CPU DrawData 和无窗口输入回归，不将此视为 GUI 视觉验收，也不结束父任务。

## Verification

- Lint：PASS。独立 `git -c diff.ignoreSubmodules=all diff --check` 退出 0；国际化日志 en-US、zh-TW 均 370/370 PASS。工程没有此次应运行的独立 C++ lint/typecheck 脚本，以项目编译与现有检查验证。
- TypeCheck：PASS。独立读取 `Inkeys/Cache/titlebar-menu-feedback-20261009/build-accepted-result.json`，ARM64 原生 MSBuild 完整 `InkeysRepo.sln Debug|ARM64`，ExitCode=0、23.6967784秒；最终 log 9 warnings、0 errors。读取构建脚本确认动态 vswhere 定位、同 PowerShell PATH workaround、关闭 node reuse、1800秒 deadline。
- Tests：PASS。独立读取 `headless-accepted-result.json`：完整 `InkeysHeadlessTests.exe --no-window` ExitCode=0、120.0476934秒、stderr 0字节；stdout 结尾 `PASS animation correctness`。
- 真实汉堡回归：scale=1、1.5、1.875 各546帧，浅深×expanded/compact/overlay 共18场景；检查透明 idle、实际 hover/held、释放仅一次开合、不导航/触发业务动作、mouseleave 背景及 alpha。采样避开 glyph、圆角、focus ring，使用真实三角形合成而不是复制按钮混色公式。
- 实际数字（三倍率相同，灰色各 RGB 通道一致）：浅色 hover=231.235、held=226.133、leave=242.875（背景目标243）；深色 hover=52.5882–53.5686、held=48.8667、leave=32.1412（背景目标32）。所有18场景 leave 填充顶点 alpha=2/255，满足 <=3；held 严格比 hover 更接近 pressed。小量余差来自既有渐近动画取整，未掩盖反馈缺失。
- 独立核对 `final-format-check.json`：10个tracked改动（含主代理收尾的父任务与journal文件）与 git checkout filters 基线均保持 UTF-8、无 BOM、纯 CRLF；产品差异仅版本色1行及汉堡局部样式。匹配的 renderer/UI spec 已同步两处合同。

最终构建/测试证据已实际读取；复用成功验证，不无意义重启构建。父任务保持进行中，本轮新修改保持未提交。
