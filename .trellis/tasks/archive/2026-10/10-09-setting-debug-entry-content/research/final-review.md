# 独立审查（2026-10-09）

## Findings (fixed)

未发现需要修正的本轮源码问题；本审查没有修改产品、测试或规范代码，只新增此独立记录。

## Findings (not fixed)

无未解决的本轮代码问题。

实际 Release 构建、HWND/DX11 最终呈现、真实鼠标拖动/双击/贴靠及跨屏 DPI 未执行，按本轮授权留待人工验收。这些是验证边界，不视为源码缺陷。IDT_RELEASE 可见性结论来自实际宏域检查。

## 实际路径与规范核对

- Setting.Layout.h 的同一 TitleBarGeometry 提供 Debug/theme/version/caption 矩形；Setting.cpp 的 HitTestSettingClientTitleBar 直接用该矩形保护 HTCLIENT，窗口控件仍返回各自系统命中值，其他标题区域仍 HTCAPTION。32 DIP 槽和 8 DIP 间距只将 theme/version 左移，caption 坐标公式保持。360 DIP 边界让标题文字与图标后空白让位；不绘制反向标题 clip。既有几何回归保留 96 DIP 拖动预算与多倍率检查。
- RenderDebugButton 使用 Bug glyph 和实际 InvisibleButton，保留 hover/active/selected 底色，无文字/tooltip。导航模态存在且鼠标位于 Debug 时局部恢复标题栏命中，随后恢复 HoveredWindow；NoFocus 避免按下阶段改变 popup 焦点。激活关闭 overlay/flyout 并清空蓝条，再通过原 navigateTo 完成滑块离页保存及稳定目的地同步；重复目标不会重播正文补间。同步不每帧关闭 Debug 页重新打开的导航浮层。
- ClearIndicator 清除 item/window 锚点、坐标和 X/Y tween；Debug 渲染不产生选中导航项，因而不提交蓝条。返回普通页遇到首个有效锚点直接 Reset，后续普通页继续既有 Target/Advance、滚动补偿和圆头绘制。真实三倍率连续帧覆盖浅深、动画开关、展开/紧凑/窄窗、快速反向和模态菜单点击。
- footer 实际绘制和纯布局均移除旧 Debug 入口/预留，不只隐藏文字。固定四个底部动作及 8 DIP inset 保留，中段获得释放高度。测试不再引用废弃 footer-debug 几何接口。
- 四项 consoleToggle 的 ID、i18n 标题/说明、Experimental UI 字段、config 保存字段和 QueueConfigWrite 与 HEAD 中原内容一致，顺序 TouchArea/PptCOM/Draw3/Cursor；唯一位置迁入 tab9。入口和四项实际源码预处理条件栈为空，其他 DRAW3_RTS_DIAGNOSTICS 与 IDT_RELEASE 分支保持。
- Display Snapshot 在诊断页一次获取、位于诊断条件宏外；拓扑及所有监视器字段/target/物理失效原因均来自该份快照。原字段和顺序保持，仅将段落合并传给 RenderEdidCard，缺快照提示保持。
- RenderEdidCard 以当前可用宽度扣真实 CardPadding，再用产品 TextHeight/TextAt 共用的 Lines 排版测量，约束完整当前帧高度并声明文本 content size，避免 AutoResizeY 延用上一帧。BeginCard 继续现有主题/边框/padding。六倍率真实 DrawData 测试检查首帧及宽度/文本变化、卡片 ScrollMax 为 0、最终文字边界和外层页滚动；长文本从宽到窄、再变短时高度即时重测，未复制测量公式作为预期结果。
- rendering-and-ui.md 已同步标题栏命中/顺序、Debug 蓝条与路由、无 footer 入口及 EDID 全高合同；检查规范对比与实际路径一致。改动未触及 vendor、公开 Setting API、配置 schema、工程/工具链或线程生命周期。

## Verification

- Lint：PASS，独立重新执行 pwsh -NoProfile -File Scripts/i18n.ps1 check，en-US/zh-TW 各 370/370；git -c diff.ignoreSubmodules=all diff --check exit 0。仓库此 C++ 任务无单独通用 lint 命令，采用适用静态检查。
- TypeCheck：PASS，核对本轮最终完整 InkeysRepo.sln Debug|ARM64 编译与链接日志/退出码，复用实际 final build（exit 0，27.8279769 秒）。ARM64 原生 MSBuild 动态定位；同调用 PATH workaround、禁用 node reuse 和 1800 秒上限已记录。此 C++ 项目的类型验证由编译承担，无另设类型工具。9 条现有 vendor warning（hashlib++ C4267、HiMsg C4201），无本轮源码 warning。
- Tests：PASS，核对最终 InkeysHeadlessTests.exe --no-window 全进程 exit 0，114.2260286 秒，stderr 0 字节，末行 PASS animation correctness。EDID 每倍率 40 帧，倍率 1/1.25/1.5/2/1.875/4；Debug 导航在 1/1.5/1.875 各 541 帧，原分层导航各 684 帧/24 次导航仍通过，共享悬停颜色 0 violations。
- 编码/范围：已核对 encoding-audit.json 各修改文件 UTF-8 有效、BOM 保持、纯 CRLF；git diff 仅本轮局部产品/回归/规范及任务记录，无大面积格式或无关产物差异。
- 检查环境：plain git diff --check 触发 HiMsg 子模块 .git/modules/HiMsg 只读权限错误；仅在该次命令指定 diff.ignoreSubmodules=all 后主仓 diff 检查通过，没有修改 Git 配置或子模块。该读取限制不影响实际完整构建及无窗口测试。

实际证据：Inkeys/Cache/debug-entry-20261009/build-final-result.json、build-final.log/stdout/stderr、headless-final-result.json、headless-final.stdout/stderr，以及本任务 research/console-release-audit.json、encoding-audit.json、implementation-verification.md。审查未改源码，因此未重复已通过的完整构建与整套测试。没有启动 GUI/Computer Use，没有 commit/push。
