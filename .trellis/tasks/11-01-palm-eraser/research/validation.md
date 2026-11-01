# 手掌橡皮实现与验证记录

日期：2026-11-01。用户已批准最终规划，后续补充的EDID失效100×100系统DIP回退已纳入实现。当前代码、完整ARM64 Debug构建及下列无窗口验证完成；未启动主窗口、设置窗口、隐藏HWND验收或Computer Use，未commit/push。

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
| 完整InkeysRepo.sln Debug\|ARM64 | exit0，0错误，3分10.47秒 | [构建日志](../../../.runtime/palm-eraser/solution-debug-arm64.log)，原生ARM64 MSBuild；PptCOM/Tests、Inkeys及Headless均构建 |
| Headless目标先行构建 | exit0，0错误 | [日志](../../../.runtime/palm-eraser/headless-debug-arm64-unsandboxed.log)，先排除设置/策略/配置编译问题 |
| InkeysHeadlessTests --no-window --setting-eraser-only | exit0 | 末行settings=0 speed=0 attributes=0 palm=0，Palm策略129项0失败；真实ImGui输入/三语/多DPI/像素预览 |
| InkeysHeadlessTests --no-window | exit0，PASS animation correctness | [全量日志](../../../.runtime/palm-eraser/headless-all-no-window.log)，含更新后的129项策略与设置回归 |
| Inkeys --draw3-palm-eraser-test | 真实进程exit0 | [stderr](../../../.runtime/palm-eraser/palm-production-stderr.log)，生产CPU转换、精确所有权、观察准入、截止、真实模型接续；无GPU/HWND |
| Inkeys --config-boundary-test | 真实进程exit0 | [stderr](../../../.runtime/palm-eraser/config-boundary-stderr.log)，真实ReadAll/Mini/写盘/严格类型/宽整数/赋值/Reset/NoUpload、七工具及缺省大屏EDID失败下DIP激活 |
| Inkeys --draw3-control-fence-test | 真实进程exit0 | [stderr](../../../.runtime/palm-eraser/control-fence-stderr.log)，真实续接/终态复用、四设备四竞争窗口/截止/idle唤醒、工作区准入 |
| Inkeys --draw3-parked-desktop-exit-test | 真实进程exit0 | [stderr](../../../.runtime/palm-eraser/draw3-parked-desktop-exit-test-stderr.log)，生产CPU封存/退出快照、真实模型生命周期/失败清理及内容proof |
| Inkeys --draw3-whiteboard-storage-test | 真实进程exit0 | [stderr](../../../.runtime/palm-eraser/draw3-whiteboard-storage-test-stderr.log)，100页、缓存引用释放、冷Undo/Redo、主题、旧代淘汰和失败 |
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
