# 实现与验证记录

## 当前结果

本次用户明确批准实现后，已完成默认多行说明、空间不足时同一卡片内下方通栏说明及可反向切换动画。源码重新完成完整 Debug|ARM64 Solution 构建和完整 --no-window 回归。Trellis 独立 check 未发现需要修复的问题，详见 final-review.md。

验证完成时源码保持未提交；随后用户明确授权提交本次改动，但要求暂不结束任务。不启动 GUI/Computer Use，不 push，不自动归档当前任务或结束父任务。此前暂停阶段的试做产物不作为当前验收证据。

## 实际行为与修改边界

- 共享 SettingRow 使用真实字体/操作自然宽度计算预算，默认完整多行说明与操作并排。说明剩余预算小于 280 DIP 时转到同一卡片下方通栏，返回需达到 296 DIP；现有小于 160 DIP 的极窄堆叠回退保留。这些是项目参数，不是微软规范断点。
- 复用 NavigationTween，160 ms / 现有动画速率；先下移避开操作，再扩展说明宽度，反向先收窄再移回。稳定行 ID 的状态保存在当前 ImGui 窗口 StateStorage，关闭动画即时到位，首次显示直接到当前模式，页面缺席/会话恢复时不计入休眠时间。
- 当前文字真实换行决定卡片高度及后续卡片占位；不缩小字号、截断或折叠说明。换行行数离散变化，自动化验证包络完整，未用空矩形插值或裁文字制造平滑效果。
- 置顶间隔直接使用现有 TopWindow.E 完整国际化说明，移除该设置的独立详情块；七个选项、索引、配置字段和原有写盘/运行时事件处理不变。其他真实详情/子设置不改。
- 产品修改限于 Setting.Design.h、Setting.Controls.cpp/.h、Setting.Pages.cpp 和 Setting.cpp；回归增补在既有 setting_design_tests.cpp。没有修改项目配置、依赖、字体或语言资源。

## 自动化覆盖

使用项目实际简中、繁中、英文资源和捆绑字体（繁体含原有简体回退），浅/深主题，100%、125%、150%、187.5%、200%、400% 六种倍率。

通过真实 SettingRow、ImFluent 控件与 ImGui DrawData 连续帧验证：

- 滑动条、按钮＋下拉、自然宽度和可换行的高按钮组；按钮组另覆盖公共 ButtonGroupRow。
- 连续缩窄/加宽、仅操作预算改变、极窄堆叠；实际说明已扩宽后中途反向；280–296 DIP 回程区轻微抖动。
- 首次显示、动画关闭、速率变化、页面缺席恢复和恢复帧零 delta；按窗口/稳定行 ID 隔离状态。
- 标题/说明/图标/真实控件互不重叠，控件包络与右对齐，说明每个可见字形恰好一次，实际文字顶点落在包络与裁剪内。
- 真实当前卡片 item 占位和下一张真实卡片起点覆盖说明高度；每帧唯一操作回调，无交互不产生事件或改变滑动值/选项。

说明文字顶点采样截止到唯一 action 回调入口，避免把同色 Combo 箭头误计为说明字形；字形计数和裁剪检查仍保持严格。业务写盘链通过源码核对与原有全量回归验证，不另建重复测量控件或持久化布局字段。

## 本次验证结果

| 验证 | 结果 | 证据 |
| --- | --- | --- |
| InkeysRepo.sln，Debug / ARM64，ARM64 原生 MSBuild | 退出码 0，81.5 秒 | Inkeys/Cache/multiline-card-layout-20261008/build-1.stdout.log、build-1.stderr.log |
| 刚构建的 Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window 完整回归 | 退出码 0，98.6 秒；stderr 为空，最终 PASS animation correctness | 同目录 headless-1.stdout.log、headless-1.stderr.log |
| Scripts/i18n.ps1 check | 通过，en-US 和 zh-TW 均 370/370 | 主会话与独立 check 的输出 |
| git diff --ignore-submodules --check | 通过，退出码 0 | 最终差异检查 |
| 六个源码/测试文件编码与换行 | 保持原 UTF-8 无 BOM + CRLF，无裸 LF | 与原格式核对及独立 check |
| Trellis 独立 check | 没有新增缺陷需要修复 | research/final-review.md |

构建从当前 Visual Studio 安装动态查找 ARM64 原生 MSBuild；在同一个 PowerShell invocation 先 Remove-Item Env:PATH -ErrorAction SilentlyContinue 并设置 MSBUILDDISABLENODEREUSE=1。按现有 Solution 正确解析 PptCOM/Inkeys 依赖，超时预算 1800 秒；为本机内存压力仅使用命令参数 /m:1 /p:CL_MPCount=1 /nr:false /v:minimal，不改项目。编译中既有 HiMsg C4201 / hashlib C4267 警告与本次变更无关。

受限 runner 的临时目录/配置写入失败与源码无关；最终完整回归在获自动审批的无窗口执行环境运行通过。早期候选说明采样把 Combo 箭头算进文字，已修正采样边界，未放宽字形/裁剪要求。上述问题不再影响当前结果。

## 验收边界

没有启动应用主窗口、设置窗口、调试 GUI、浏览器或 Computer Use。自动化通过能证明真实文字/控件包络和连续帧状态，不能证明实际 HWND/DX11 窗口中的视觉节奏、换行时卡片高度观感或用户对阈值的主观满意度；这些需要另行明确授权的 GUI 验收。当前请求授权范围内的实现和非 GUI 验证已完成。

## 用户人工验收与任务结束（2026-10-09）

用户明确确认“人工验证完成，对于这个动态卡片的任务可以结束并提交 commit”。据此，本子任务的人工验收已由用户完成，授权归档及提交收尾记录；详见 manual-acceptance.md。以上自动化阶段未进行代理 GUI 检查的事实不变。实现已提交为 5aaf7690，父任务继续 in_progress，不 push。
