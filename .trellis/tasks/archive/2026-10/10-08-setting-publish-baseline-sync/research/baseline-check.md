# 发布内容整合独立复核

日期：2026-10-08。角色：trellis-check。来源固定 `e2c212d4d9cd68367c08363c450d392e9d63bcef`；隔离工作区 `codex/fluent-setting-sidebar`。已读取 check.jsonl、PRD/design/implement、所列桌面规范和来源/运行时研究。

当前状态：复核完成，可进入依赖此基线的侧栏子任务。常规页四个原标题已补齐，第二次完整 Solution 和第三次最终增量 Solution 均退出0；最终重新编译产物的无窗口回归退出0。首轮 vcpkg 前置环境失败与后续源码验证分开记录，不作为生产编译通过证据。

## Findings (fixed)

检查期间主工程由 runtime_merge 唯一构建，Checker 没有并改冻结源码或另起构建。以下源码问题已报协调者并由相应所有者修复；Checker独立修正文档中的来源事实。

- `Setting.cpp`：PPT ResetPositions 后漏显式 PptComWriteSetting。`Ppt.cpp::ResetPositions` 在 helper 未 initialized 时只捕获新位置，必须由设置 FIFO 保存；已经恢复发布调用。
- `Setting.cpp` / `Setting.SessionState.h`：旧同步逻辑通过未保存 Record 的 if/else if 推断源，中组连续拖动第二帧可能被上一帧底组值回写。现在真实 Slider changed、Reset、Joined 事件传给生产 `ResolveLinkedPptScaleDraft` 决定同步方向；两组即时更新运行时/Notify，松手 FIFO 保存。新增 headless 直接调用该生产 helper，覆盖连续中/底拖动、下一 idle 帧、同值 Reset、取消同步、新加入同步及不同初值静止。
- `Setting.Pages.h`：内部 dynamicEdgeLighting 默认已改为发布 false，生产初始化仍取配置值。
- `Setting.Pages.cpp`：四个常规分组恢复发布原标题，分别使用 Regular.StartUp.N / Appearance.N / Behavior.N / Tentative.N；已逐项静态核对。
- `Setting.cpp`：恢复独立 ManualUpdate 原文字/入口；恢复 DDB 全用途/GPL/仓库/质量保证/后果说明，以及17个拦截目标和组件原说明。
- 三语言 JSONC：主页不再介绍已经删除的智能绘图；保留 Fluent 自适应主页和主题框架。
- `research/content-source-audit.md`：Checker 修正只看 source:4892 的不完整判断。source:4715/4830 Slider 块已即时更新运行时，Reset/Sync 同样即时应用；4892 只是松手补写/Notify，不应推导为延迟应用。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：协调者同步自然行累计布局与两组 PPT 缩放时机/事件合同，删除旧固定卡片高度和三组描述。

## Coverage

- 17个实际视图逐页核对：主页、语言、配置、版本、CI/CD、常规、绘制、插件概览、PPT、SuperTop、快捷方式、DDB、组件、快捷键、实验、赞助、调试。预设/性能路由与主页入口已删除，Plugins及后续旧编号8..14保持，6/7空洞无可达入口。
- 版本页包含完整12种更新状态，保留原检查/修复/手动下载/立即更新动作；更新渠道与架构更改仍冻结快照通过 FIFO。
- 动画速率、动态光影、帧率隐藏不覆写持久偏好；Bar缩放/动画速率实时同步后松手保存；Setting倍率保留Fluent松手即生效及字体重建，三语言说明一致。
- 橡皮取同一整页快照，独立总开关不改五入口kind；Pen响应隐藏Auto后仍保存稳定枚举；TouchArea更新Host及main配置。控制台四项/DComp遵循发布配置，临时模型/标尺/beta不写正式配置。
- 16组件均在保存后即时同步UI3；DDB动作与配置快照保持发布顺序；主题 bool 默认/严格读取/捕获正确。
- `Other.Config.cpp/.cppm`归一换行后与固定publish逐字一致。Window对publish的差异仅Fluent窗口frame补充；受控owner、焦点三态、常驻显隐/epoch/字体/主题及FIFO保留。

## Findings (not fixed)

- 既有LnkHelper同步桌面I/O：`Setting.cpp`两个开关回调直接 `shortcutAssistant.SetShortcut()`；`IdtPlug-in.cpp:1312`同步枚举/删除桌面lnk并调用ShellLink COM。publish和旧Fluent均有该路径，不是本轮merge新增。调整需要冻结路径/开关并收口helper接口/线程，按主代理决策保留到09-06父任务；本轮不声称所有原有阻塞业务已消除。该helper的COM初始化亦为既有注释状态，创建快捷方式FIFO的真实COM可用性需后续验证。
- 发布源已有DDB/组件/实验/调试硬编码中文。现有资源键三语言全部通过，不等价于所有硬编码页面已国际化；此次保持发布内容，不另扩翻译功能。
- 当前不启动GUI/Computer Use/任何HWND测试；真实鼠标、完整页面截图、DPI跨屏、DWM/backdrop、Office/窗口焦点与磁盘实际副作用保持待实机验收。

## Verification

- Lint/static：自有Setting/config/Window/i18n/生成/测试文件 `git diff --check HEAD -- <paths>` 退出0；3个项目/filters XML通过，无重复ClCompile登记；核心文件UTF-8无BOM/全CRLF。
- i18n：PASS。Checker独立运行 `Scripts/i18n.ps1 check`，en-US及zh-TW均366/366，100%通过；runtime_merge在最终增量和回归后再次check退出0，仍为366/366。
- TypeCheck/Build：PASS。`InkeysRepo.sln Debug|ARM64` 使用动态定位的 ARM64 native MSBuild，同一invocation规范Path、/m:1 /nr:false、900秒允许。第二次完整编译486.7秒、BUILD_EXIT=0；包含最终四个原标题的第三次增量13.0秒、BUILD_EXIT=0。证据：`solution-build-second-stdout.log` / `solution-build-second-stderr.log`、`solution-build-final-stdout.log` / `solution-build-final-stderr.log`；Checker读取最终日志确认主程序与Headless产物输出。
- Tests：PASS。最终重新编译的 `InkeysHeadlessTests.exe --no-window`，HEADLESS_EXIT=0，末尾为 PASS animation correctness，日志 `headless-no-window-final.log`。测试链包含直接调用生产helper的 CheckLinkedPptScaleDraft，以及既有设置session、配置/橡皮/绘制等无窗口回归；不沿用修前产物，不执行HWND/GUI测试。
- 全量merge diff含29文件832行既有空白诊断，已由协调者核对均来自未修改的固定publish blob（`inherited-whitespace.json`）；遵守minimal diff不清理历史格式。本轮自有文件检查通过。
