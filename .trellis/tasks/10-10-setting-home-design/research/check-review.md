# 独立 Trellis 审查

审查依据：check.jsonl 列出的 Native Desktop 规范、prd.md/design.md/implement.md、用户批准的主页计划及实际工作区 diff。审查者先只读，完整构建结束后在主会话许可的安全检查点进行以下局部修复。未改网站/vendor/其他页面，未运行 GUI、commit 或 push。

## Findings (fixed)

- File: Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp
  - Issue: 新增主页代码调用 ImFluent::GetColorU32，但当前包含链只有公共 imfluent.h。该函数实际声明在 imfluent_internal.h，完整 ARM64 构建在本文件报 C2039/C3861。
  - Fix: 增加本文件对 imfluent_internal.h 的直接 include；沿用相邻 Setting.Shell.cpp 的既有用法，不更改库接口或 vendor。
- File: Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp
  - Issue: 新增横幅渐变的三个 0..255 通道由 ImU32 隐式转 float，产生本次新增 C4244。
  - Fix: 为范围明确的三个通道补充 static_cast<float>，并把四处新 Pixels 条件表达式改为 float 字面量。不改变数值或混色结果，不清理无关 warning。
- File: InkeysHeadlessTests/setting_design_tests.cpp
  - Issue: 新回归验证鼠标轻点、滚动取消和命中框，却未实际发送键盘输入验证新增控件的焦点/激活合同。
  - Fix: 主页 fixture 局部启用并恢复 NavEnableKeyboard；对实际绘制和轻点定位得到的每个许可证名称片段、GitHub/GitCode 和 Star/Sponsor 控件，检查 retained NavId，再通过真实 Tab 进入键盘导航（同目标、显示焦点、不产生动作），然后通过真实 AddKeyEvent Enter/Space 验证一次正确动作、可见焦点及空闲不重复。没有直接赋值 NavActivateId，没有弱化已有断言。另将本次测试中的 inset 条件字面量改为 28.0F / 20.0F，消除新增 C4244。

- File: Inkeys/Inkeys/UI/Setting/Setting.Pages.cpp
  - Issue: DrawInline 在 InvisibleButton 之后再次调用 RenderNavCursor。当前 ImGui 1.92.7 的 InvisibleButton（imgui_widgets.cpp:859）已绘制一次，造成双份焦点几何；现有页面 alpha 淡入中会发生叠加。
  - Fix: 删除页文件中的重复调用，保留原生 InvisibleButton 的焦点绘制。没有更改 vendor、导航语义或样式。
- File: InkeysHeadlessTests/setting_design_tests.cpp
  - Issue: 第一次 --no-window 暴露 fixture 的两个错误前提：鼠标点击留下 NavId 但原生规则隐藏 NavCursorVisible；320 DIP 窗口并非每种语言都拆分 GPL 名称。该次失败只有 192 个键盘入口断言与 1 个无条件多行断言，其他回归通过。
  - Fix: 按 imgui.cpp:13767 的实际激活门禁，发送真实 Tab 进入键盘导航后再保持原严格 Enter/Space 断言，不直接设置 NavId/NavActivate 或焦点显示字段。以实际 Caption 全名称 TextWidth 与版权内宽判断必需拆行，并新增至少一个真实多片段 case 的聚合覆盖；所有实际发现的名称片段仍逐个验证点击、键盘和拖动取消。
## Findings (not fixed)

未发现其他需要修改的产品代码问题；本轮没有留下未修复的设计/API/模块边界问题。

- 原生 HWND/DX11 呈现观感和实机触摸未执行。这是当前授权的无窗口验证边界，不能从 CPU DrawData 通过推出实机视觉验收；留给用户后续人工审阅。

## 审查证据

- HomeAction 的 License/GitCode 追加在原枚举末尾，既有值不变。Setting.cpp 中两目标 URL 与 PRD 一致；ShellExecuteW 在该业务协程宏域进入原异步队列。RenderHome/测量/字标头没有 Shell 或写配置副作用，Star/Sponsor 复用 GitHub/Support。
- 三语言欢迎语、标语分段、完整版权年份/署名/许可/代码托管说明和按钮文案逐项对照 PRD；英文同时有 Chen Runlin 与 Chen Yunlam。全部新增显示名称来自 i18n 源，sync 生成 header 和基准 snapshot 与源一起更新。
- MeasureInline 先按真实全文/font 测量，再按源 span 与行号登记局部名称链接，文本前后说明不是链接；InvisibleButton EnableNav、其原生 RenderNavCursor 和 AllowContentDrag 接入当前输入规则。新按钮同样调用 AllowContentDrag，鼠标越过阈值时由现有 BeginDragScrolling 转移 capture、释放不路由。
- Setting.HomeBrand.h 六组 cubic、origin、0.14 右倾、0.8 采样、4.2 秒速度宽度模拟和两套九点彩虹色与只读网站 NewHome3/glyphs.ts、softPen.ts、styles.ts 对照一致。Geometry 静态 const 缓存建模，Draw 仅缩放/平移；连续变宽轮廓、透明外缘和圆接头均缓存。bounds 按完整 geometry 包含 y 下伸，字标 80%/520 DIP 预算和 8 DIP 圆角/28或20 DIP inset 符合设计。
- HeroBackground 只重着色本次圆角 fill 的顶点并保留 AA alpha。局部浅蓝/薄荷/淡紫以及深蓝灰 palette 不污染其他卡片。版权/开源卡高度来自实际字体包络，按钮列预算保留左文 280 DIP/间距24 DIP；窄宽下移。Dummy 占位后恢复实际 cursor，外层页负责滚动。
- 探索、常用设置、作者/反馈、footer、教程图像和原动作区域的产品实现保留。原主页滚动测试改用实际 feature DrawData 位置定位，溢出消失用足以容纳新内容的 2400 DIP viewport，既有动作/scroll/assertion 保留；新 RenderHome 回归覆盖真实三语言/字体、五窗口宽/导航状态/浅深和七倍率。
- 新 header 在 vcxproj/filters 正确登记，没有新增构建系统、设备、纹理、配置或依赖。
- 主会话已更新 rendering-and-ui.md 末尾首页合同，与当前实现边界一致；其元数据和最终验证记录由主会话负责。

## Verification

- Lint / static: PASS。Scripts/i18n.ps1 check：en-US 与 zh-TW 均 383/383（100%）PASS；git diff --ignore-submodules=all --check PASS。普通 git diff --check 曾因只读沙箱不能遍历 HiMsg 子模块 gitdir 而失败，使用 ignore-submodules 检查本任务实际 diff，不修改子模块或 Git 配置。
- TypeCheck / compile: PASS。主会话使用 ARM64 原生 MSBuild 和同 invocation PATH workaround，完整 InkeysRepo.sln Debug|ARM64 首次通过 ExitCode 0（139.035 秒），焦点修复后增量通过 ExitCode 0（38.8144011 秒）。最新测试 fixture 修复后的完整 Solution build-final-tests ExitCode 0（83.2892152 秒），0 error、9 个既有 HiMsg/hashlib++ warning；本次新增 warning 已清理。先前 runner/PDB 环境错误与本地缺 include 源码错误分别记录，不混淆。
- Tests: PASS。最新完整 InkeysHeadlessTests.exe --no-window（headless-final）ExitCode 0，182.2121846 秒，stderr 为空，stdout 最终 PASS animation correctness / all modules passed。新主页 scale1 跑 2053 帧，48 次真实鼠标轻点 + 96 次真实键盘激活，共 144 个正确动作；其余六倍率各 480 帧，所有回归通过。第一次 headless-reviewed 的 fixture 失败（ExitCode 1，164.7229729 秒）按上述原生导航与字体度量原因修复后再完成完整验证，没有删除或弱化严格行为断言。所有构建/headless 由主会话串行执行，审查者未并行启动验证产物。
- Encoding / EOL: PASS。主会话审计 15 tracked + 10 new 的原编码/CRLF 均通过；审查者在 fixture 修复后再次核对该测试文件，strict CRLF PASS。新报告 UTF-8 无 BOM / CRLF。
