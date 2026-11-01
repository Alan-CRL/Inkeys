# 手掌橡皮实现与验证记录

日期：2026-11-01。用户已批准最终规划，后续补充的EDID失效100×100系统DIP回退已纳入实现。初版代码、完整ARM64 Debug构建及下列无窗口验证完成，随后按用户要求提交d003fae1，未push；未启动主窗口、设置窗口、隐藏HWND验收或Computer Use。本轮页面反馈修订的验证另列于文末。

## 实现结果

- 触摸面积/比例在落笔窗口内达标即转换，默认150ms（推荐），可选100/250/500ms；最大有效消费样本定尺，截止或首次物理Up固定，旧擦除轨迹不追溯变粗。
- 可信物理基准25cm²；EDID/物理标尺不可用仍可使用，基准改为100×100系统DIP=10000DIP²，继续按面积倍率25%–400%调节并显示Warning。缺省大屏默认开启不依赖EDID成功。
- 本触点原工具临时几何撤回、其他内容保留；原实际工具已是橡皮时保留旧擦除。相邻抑制、非相邻多手掌、严格新独立单Touch及边擦边写开/关已接入。
- 真接续优先，继承旧工具和同一手掌尺寸，不开新识别窗；普通绘制遇大面积接续仍绘制。失败/超时走新独立接触。
- 新详情页、七工具（含正常可勾选Brush）、时间下拉、面积滑块同条目重置、等面积像素实尺预览、三语与NoUpload配置完成。
- 沿用整笔抬笔/完成提交Z序、Undo/Redo、现有文档与UInk；没有重启已停止的真实时序覆盖研究或增加私有时间协议。

## 动态验证

| 验证 | 结果 | 证据/范围 |
| --- | --- | --- |
| 完整InkeysRepo.sln Debug\|ARM64 | exit0，0错误，3分10.47秒 | [构建日志](../../../../../.runtime/palm-eraser/solution-debug-arm64.log)，原生ARM64 MSBuild；PptCOM/Tests、Inkeys及Headless均构建 |
| Headless目标先行构建 | exit0，0错误 | [日志](../../../../../.runtime/palm-eraser/headless-debug-arm64-unsandboxed.log)，先排除设置/策略/配置编译问题 |
| InkeysHeadlessTests --no-window --setting-eraser-only | exit0 | 末行settings=0 speed=0 attributes=0 palm=0，Palm策略129项0失败；真实ImGui输入/三语/多DPI/像素预览 |
| InkeysHeadlessTests --no-window | exit0，PASS animation correctness | [全量日志](../../../../../.runtime/palm-eraser/headless-all-no-window.log)，含更新后的129项策略与设置回归 |
| Inkeys --draw3-palm-eraser-test | 真实进程exit0 | [stderr](../../../../../.runtime/palm-eraser/palm-production-stderr.log)，生产CPU转换、精确所有权、观察准入、截止、真实模型接续；无GPU/HWND |
| Inkeys --config-boundary-test | 真实进程exit0 | [stderr](../../../../../.runtime/palm-eraser/config-boundary-stderr.log)，真实ReadAll/Mini/写盘/严格类型/宽整数/赋值/Reset/NoUpload、七工具及缺省大屏EDID失败下DIP激活 |
| Inkeys --draw3-control-fence-test | 真实进程exit0 | [stderr](../../../../../.runtime/palm-eraser/control-fence-stderr.log)，真实续接/终态复用、四设备四竞争窗口/截止/idle唤醒、工作区准入 |
| Inkeys --draw3-parked-desktop-exit-test | 真实进程exit0 | [stderr](../../../../../.runtime/palm-eraser/draw3-parked-desktop-exit-test-stderr.log)，生产CPU封存/退出快照、真实模型生命周期/失败清理及内容proof |
| Inkeys --draw3-whiteboard-storage-test | 真实进程exit0 | [stderr](../../../../../.runtime/palm-eraser/draw3-whiteboard-storage-test-stderr.log)，100页、缓存引用释放、冷Undo/Redo、主题、旧代淘汰和失败 |
| Scripts/i18n.ps1 check | 通过 | en-US与zh-TW各590/590，中文基准及生成头/snapshot同步 |
| 格式/范围 | 通过 | git diff --check、project XML解析；22个原文件BOM/UTF-8/CRLF与baseline一致，新源码保持CRLF |

Windows GUI子系统的早期CLI用Start-Process -WindowStyle Hidden -Wait -PassThru及显式stdout/stderr重定向捕获真实ExitCode，未把shell提前返回或空日志作为通过证据。

## 审查与修复

独立交叉审查见[review.md](review.md)。已解决Config测试误用已删除的复制构造、非并行手掌强制收尾Laser遗漏生命周期/轨迹冻结、以及Palm Fixed长静止后模型超过单次输出上限无法续擦的问题。后两项复用正常终态/模型重锚的局部helper，新增生产探针覆盖，普通Fixed和速度控制语义不扩展。

## 本机/Codex环境与警告

首次沙箱内Headless构建因C1902在各编译单元1:1失败。全新一行C++、全新PDB、同一v143 ARM64工具也在沙箱内失败，沙箱外同命令exit0，说明是本机Codex沙箱的PDB运行/通信限制，而非本次源码。实际MSBuild据此在沙箱外完成；未修改SDK、依赖、工具链或DebugInformationFormat来绕过。

完整构建有140条现有第三方/原范围警告，0错误；没有为清零而修改无关代码。Vcpkg app-local阶段在规整后的PATH找不到pwsh时，按既有目标回退到绝对路径WindowsPowerShell并成功，构建正常结束，不是漏跑依赖阶段。

原编译器安装与PDB匹配问题的一般说明见[Microsoft C1902文档](https://learn.microsoft.com/en-us/cpp/error-messages/compiler-errors-1/fatal-error-c1902?view=msvc-170)；沙箱归因以本次同工具、同源文件、独立新PDB的对照退出码为证据。

## 未执行与实际限制

- 未授权GUI/真实触摸硬件/多人实机测试，未启动相关窗口。CPU helper probe和ImGui回放不能等同完整Host/Run/GPU像素或设备手感验收。
- 设备必须提供可解释的触摸面积；无有效元数据不能用压力伪造。阈值使用既有合并输入实际消费的最大样本，不保证原始硬件包峰值。
- EDID失效的100×100系统DIP为确认的常规大屏工程基准，仍可能与真实厘米尺寸不一致；用户可用面积倍率调整，系统DPI与设置UI倍率口径分离。
- 偶然擦写重叠不提供真实时序互盖，继续使用原抬笔/提交顺序。

全量旧测试自动生成的10张eraser-demo PNG已移到当前任务忽略的runtime目录留存，未在其他旧Trellis任务留下无关新文件。


## 2026-11-01 页面反馈修订验证

- 页面删除介绍块；Warning紧跟页标题；基本设置首项为总开关。
- 预览改为单个主题accent圆角空心边框；悬浮下拉默认1:1，可选4:3、2:1、1:2。切换只改页面状态，面积仍按绘制输出原始像素；保留原Combo透明度，按用户要求允许边框透出，不加不透明底色。
- 倍率10..400同步设置滑块、策略归一化和配置严格读入；默认/重置100不变。配置覆盖10/24/25/100/400，非法9/401回退；137项策略含10%实际识别边界均通过。

| 最终验证 | 结果与证据 |
| --- | --- |
| 完整InkeysRepo.sln Debug ARM64 | exit0，0错误，17.08秒；[最终构建日志](../../../../../.runtime/palm-eraser/ui-feedback-build-final.log)。现有第三方6条warning；本轮新增索引符号warning已修正 |
| 完整InkeysHeadlessTests --no-window | exit0，PASS animation correctness，6分11.74秒；[完整日志](../../../../../.runtime/palm-eraser/ui-feedback-all-no-window.log)。含真实ImGui多语言/主题/DPI、4比例与实际描边/空心/控件操作、位置顺序、七工具、超宽查看、137项Palm策略及原有回归 |
| Inkeys --config-boundary-test | 真正进程exit0；[日志](../../../../../.runtime/palm-eraser/ui-feedback-config-stderr.log)，全量/Mini/写入/赋值/默认/非法类型及10..400范围 |
| i18n check、任务context、格式 | 三语590/590；5+5条context校验通过，UTF8/BOM/CRLF保持，git diff --check通过 |
| 独立审查 | [审查记录](ui-feedback-review.md)，本轮全部产品/策略/配置/测试diff；无未解决问题 |

### 验证环境与已解决的测试问题

默认Debug输出首次链接LNK1168，Inkeys.exe被占用；保留现有程序，使用命令行OutDir=Build/ARM64/PalmUiValidation/对同一Solution/Debug/ARM64构建，不修改工程文件或工具链。ARM64原生MSBuild通过vswhere发现，沿用同PowerShell invocation的PATH规整；此前已证实的沙箱PDB限制仍通过沙箱外构建处理。

初次设置测试的页面文字顺序断言误用了ImGui文字日志；Design::TextAt直接AddText不入日志，故改为实际控件矩形/Warning背景顶点比较，并结合产品源码确认标题/分组顺序。产品绘制没有为测试增加钩子。初次速度橡皮配置fixture受默认TEMP写入环境影响，最终完整回归将TEMP/TMP指向仓库忽略目录后正常通过；未改产品写盘逻辑。

全量测试生成的10张PNG从旧任务的未跟踪输出位置移入忽略目录.trellis/.runtime/palm-eraser/ui-feedback-visuals，未改变旧任务文件。过程中出现的额外Inkeys/Inkeys.aps二进制变动未由本轮编辑，已保留不动。

人工测试程序：Build/ARM64/PalmUiValidation/Inkeys.exe。本轮未自动启动主界面、设置窗口、隐藏HWND或Computer Use；真实屏幕/触摸手感仍需人工验证。本轮未commit/push。


## 第二轮反馈：居中、尺寸提示与1%下限（2026-11-01）

- 边框在容纳得下时按实际预览viewport水平居中，不乘设置DPI、不缩放；超宽继续横滚，下拉维持左上角悬浮与原透明度。
- 阈值说明精简为“提高倍率可减少误触，降低倍率更容易识别手掌。”；下方用现有SettingRow note显示标准i、cm×cm和px×px。参考cm²由同一已应用像素面积及可信mm/px面积乘积导出；非可信/估算仅px，未发布无虚构数值。四比例及当前倍率同时作用两组尺寸。
- 整数倍率范围1..400，默认/重置100%；严格配置和运行期归一化一致。测试保留旧合法值并验证1%、0/401回退及1%真实识别边界。

| 验证 | 实际结果 |
| --- | --- |
| 完整InkeysRepo.sln Debug ARM64 | exit0，0错误，1分00.41秒；[构建日志](../../../../../.runtime/palm-eraser/size-feedback-build.log)，94条现有范围warning，未通过修改无关代码清零 |
| InkeysHeadlessTests --no-window --setting-eraser-only | exit0，1分12.47秒；settings=0、speed=0、attributes=0、palm=0；Palm144项0失败。[日志](../../../../../.runtime/palm-eraser/size-feedback-settings-tests.log) |
| Inkeys --config-boundary-test | 真实进程exit0；验证1/9/10/24/25/100/400配置往返、非法类型和范围；[日志](../../../../../.runtime/palm-eraser/size-feedback-config-stderr.log) |
| i18n/格式/context/独立审查 | en-US与zh-TW各592/592；生成头及中文snapshot同步；git diff --check、5+5条context通过；13个本轮源码/资源与本轮开始快照BOM/UTF8/CRLF一致；[独立审查](ui-feedback-review.md) |

专项真实DrawData覆盖四比例cm/px文本（标准i后的Caption字形）、可信1/100/400%、估算携带旧cm参考仍只显示px、未发布无提示、蓝框与实际child InnerRect居中、1%小图、超宽滚动、真实比例/滑块1%/重置输入及无偏好额外写盘。提示参与原行自适应测高，未修改全局Controls。

使用vswhere发现的ARM64原生MSBuild、同PowerShell invocation PATH规整，构建沿既有已确认的沙箱PDB环境处理在沙箱外执行。为保留现有运行程序，只通过命令行OutDir输出至Build/ARM64/PalmUiSizeValidation；未改变工程/SDK/依赖。测试TEMP/TMP仅在该验证进程设为仓库忽略scratch，未改产品写盘。

本轮选择与改动直接相关的设置/算法/配置专项；上一轮完整--no-window已通过，本轮未重复与修改无关的HomeWARP全量基准。未启动主窗口、设置窗口、隐藏HWND或Computer Use；人工触摸/屏幕效果待用户验收。未commit/push，Inkeys/Inkeys.aps额外变动保留未触及。

人工测试程序：Build/ARM64/PalmUiSizeValidation/Inkeys.exe。


## 最后补充：边擦边写条件i提示（2026-11-01）

仅在边擦边写开启且任一多指偏好关闭时显示标准i：全局多指关闭优先显示“多指绘图已关闭，边擦边写无法生效。”；否则激光多指关闭显示“多指激光笔已关闭，边擦边写在激光笔下无法生效。”。两个多指均开启或边擦边写关闭时隐藏；手掌总开关不作为额外条件。

Setting.cpp每帧只读现有两个配置原子到draft，ToggleRow复用标准note测高/绘制；无新增配置、写入或运行期擦写行为修改。三语新增两条提示，保留之前1%倍率与实尺预览。

| 验证 | 实际结果 |
| --- | --- |
| 完整InkeysRepo.sln Debug ARM64 | exit0，0错误，2分02.87秒；[构建日志](../../../../../.runtime/palm-eraser/parallel-note-build.log)，85条现有范围warning，未做无关清零 |
| --no-window --setting-eraser-only | exit0，1分52.95秒；settings=0、speed=0、attributes=0、palm=0，144项Palm策略0失败；[日志](../../../../../.runtime/palm-eraser/parallel-note-settings-tests.log) |
| 提示专项 | 复用真实i/Caption字形验证16组master/parallel/global/laser状态，含显示/隐藏、全局优先、无编辑事件及布尔偏好保留；沿现有三语/主题/DPI矩阵运行 |
| i18n/格式/审查 | en-US和zh-TW各594/594，生成header及中文snapshot同步；9个本轮代码/资源保持基线UTF8/BOM/CRLF；diff --check及5+5条context通过；[审查记录](ui-feedback-review.md)无未解决问题 |

继续使用vswhere发现的ARM64原生MSBuild和同PowerShell invocation PATH规整；既有沙箱PDB限制在沙箱外构建处理。新输出目录Build/ARM64/PalmUiParallelValidation用于保留用户运行中的程序；未改工程、工具链或依赖，测试TEMP/TMP仅指向进程内忽略scratch。未运行GUI/HWND/Computer Use，未重复与本次提示无关的全量基准或配置probe。未commit/push，额外Inkeys.aps变动保留未触及。

最新人工测试程序：Build/ARM64/PalmUiParallelValidation/Inkeys.exe。


## 人工验收与收尾（2026-11-01）

用户明确确认人工验证通过，并授权结束任务、提交commit及清理多余构建程序。最终同一完整Solution Debug/ARM64构建到默认路径通过（exit0，0warning/0error，13.43秒），日志位于.trellis/.runtime/palm-eraser/final-default-build.log。已在核验绝对路径及无reparse后清理Build/ARM64下PalmUiValidation、PalmUiSizeValidation、PalmUiParallelValidation三个临时目录，保留默认Build/ARM64/Debug/Inkeys.exe；早期临时程序链接作为历史记录保留。人工结果来自用户确认，未由代理运行GUI。Inkeys/Inkeys.aps额外变动保留、不纳入本任务提交。
