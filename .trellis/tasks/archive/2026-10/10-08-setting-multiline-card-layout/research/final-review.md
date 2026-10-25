# 多行设置卡片最终独立审查（2026-10-09）

## 审查结论

未发现需要修复或保留未修复的本任务源码问题；本轮 reviewer 未改生产代码或测试。读取完整任务上下文、最新实现授权及实际 Setting 规范段落，审查六个源码/测试文件和主会话更新的设置行规范、父任务替代说明。

- 公共 SettingRow 按实际文字/action 预算选择行内、下方通栏说明和极窄控件换行；DIP 到物理像素只换算一次，真实占位包含当前完整文字包络和尾部间距。
- StateStorage 由当前 ImGui 窗口和稳定行 ID 隔离；实际页面/子页 child ID 含稳定路由，标题翻译不改变状态身份。补间先下移再扩宽，反向从当前值续接；280 DIP 进入预算、16 DIP 返回余量、160 ms/既有速率与代码一致。
- 页面缺席帧不推进，窗口隐藏/恢复通过既有 navigationDelta=0 排除休眠时间；首次出现直接到位，禁用动画即时归位。测量不执行 action，实际绘制仅调用一次。
- 置顶间隔使用既有完整国际化说明，移除仅该项的独立 Details；七个选项顺序、索引与原 TopSleepTime/setlist 写盘、topWindowNow 副作用保持。
- 回归在唯一 action 回调入口截取说明顶点，排除同色 Combo 箭头，仍检查全部可见字形恰好一次、实际 clip/文字包络、操作边界与下一张真实卡片。使用简/繁/英资源、浅深主题和六倍率连续帧，覆盖滑动条、按钮加下拉框、较高换行组、宽度/操作预算变化、扩宽后的反向、临界抖动及生命周期。
- 更新的 native-desktop 设置行规范与父任务替代摘要/折叠说明段落一致，未把项目阈值写成官方 WinUI 固定标准。

## 验证

- Lint/静态检查：git diff --ignore-submodules --check，exit 0；Scripts/i18n.ps1 check，en-US 与 zh-TW 各 370/370、PASS。仓库未发现独立 C++ lint 配置，不新增检查系统。
- TypeCheck/编译：主会话动态定位 ARM64 原生 MSBuild，同一 PowerShell invocation 规范化 PATH，完整 InkeysRepo.sln Debug|ARM64，/m:1 /p:CL_MPCount=1；exit 0，81.5 秒。审阅最终 build-1 日志；无本任务新增源码错误，第三方既有 warning 未扩展修复。
- Tests：使用本次新编译的 InkeysHeadlessTests.exe --no-window，exit 0，98.6 秒；最终 headless-1.stderr.log 为空。
- 六个源码/测试文件均通过严格 UTF-8 解码，无 BOM，保持 CRLF，裸 LF 数为 0；diff 未出现整文件格式变更。
- 日志位于忽略目录 Inkeys/Cache/multiline-card-layout-20261008/：build-1.stdout.log、build-1.stderr.log、headless-1.stdout.log、headless-1.stderr.log。

## 验证边界

CPU 几何、真实控件/字形顶点与连续帧测试通过，不等同于实际 DX11 呈现或窗口交互验收。本任务未获 GUI/Computer Use 授权，因此未启动窗口检查真实动画观感；换行行数变化带来的离散高度变化仍需后续获授权的视觉验收。没有 commit/push、工程/依赖配置修改或额外构建。
