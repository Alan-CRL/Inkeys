# Win7 主画布恢复

## Goal

定位并最小修复 Windows 7 SP1 + 仅 KB2670838 上 Inkeys3 主画布不可见问题。以用户能看见并使用主画布作为验收；若当前环境不能复现根因，则交付范围明确的低开销诊断候选，等待 Win7 现场结果。

## Confirmed Background

- 源码基线为 `chore/publish` / `f42083f4f0b5b969b25abc68672df0eeda070f75`。开始前工作区有 5 项未提交内容：一份既有 Trellis research 文件及 `inkStrokeModelerTest/` 下四个生成 `.cso`；均须保留。
- Win7 日志版本字符串为 `3.0.0-20260811a`，没有 Git SHA。附件 EXE 为 x64，SHA-256 `2BFB6A2B77DCBD33113D4380B6510D754B992ABB4CE0D33418C9ABFB8D2B0BEC`，与本机 `Build/x64/Release/Inkeys.exe` 完全相同；附件 `PptCOM.dll` 也与对应 Release 文件相同。该本机 EXE 时间为 2026-10-02 09:41，早于基线提交时间 10:34；可能在提交前由相同源码构建，但没有可证明源码提交身份的证据，故测试包与当前源码的对应关系仍未确认。
- 附件显示硬件设备初始化失败后 WARP / FL 11_0 成功；没有硬件初始化阶段及 HRESULT。`UlwDirtyRect` waitable swapchain 以 `0x887a0001` 失败后，普通 swapchain 明确成功启用。日志中的成功 ULW 统计属于 Bar，不能代表 Drawpad。
- 日志已到达“窗口初始化完成”和“线程初始化完成”。源码要求主流程在继续初始化前检查 Draw3 首帧就绪；这降低了“启动阶段永远没完成首帧”的可能性，但不证明主画布实际像素可见，也不提供后续显隐及主画布呈现结果。
- `ResolveDrawpadPresentationSurface` 在空白页选择模式且辅助全帧已清洁时会选择隐藏两张画布窗；当前日志没有记录该运行状态。不能将这一合法分支直接判为故障。

## Requirements

- 排查只沿主画布路径：Drawpad 与 DrawpadPresentation 窗口生命周期/显隐/尺寸/层级，Draw3 Host 首帧和门控，硬件到 WARP 初始化，DComp/ULW 现有路径及普通 swapchain 回退，主画布实际提交与错误处理。
- 诊断记录必须能区分 Drawpad、DrawpadPresentation、Freeze、Bar 和 StartupPreview，并覆盖构建/EXE 身份、硬件设备失败阶段与 HRESULT、窗口状态变化、首帧尝试与结果、门控等待原因、实际目标和最终呈现参数/错误。只补足本问题缺失的低开销事件证据，不建立通用诊断框架。
- 自动可复现的根因必须先留下修复前失败证据，再做最小修复并用同一检查验证；若现有环境无法复现，则不猜测性改变窗口策略或呈现后端，只交付诊断候选及最短 Win7 重测步骤。
- 保持 Windows 7 SP1 + KB2670838 目标；保留硬件 FL11 与 WARP 回退、`FLIP_SEQUENTIAL`、DComp/ULW、HTTP/HTTPS 和 HTTP 回退、旧 `智绘教.exe` 兼容、现有功能开关及 PPT 数据保护策略。不得通过关闭动画/光影或降低画质验收，不得改动四个生成 `.cso`。
- 主画布恢复验收在书写模式进行：启动后画布可见、首笔可见；切换选择模式后桌面输入穿透；定格切换及返回后画布仍可用。PPT 只做有限回归，不重开整套重构。
- 每个候选交付记录源码身份、EXE SHA-256、架构和配置，并给出操作步骤、期望现象和需回传日志。未运行的 Win7 检查明确标为“待用户实测”。

## Acceptance Criteria

- [ ] 记录并保留初始 HEAD、工作区、既有构建产物、测试包 EXE/DLL 身份；不 reset、clean、commit、push 或覆盖用户未提交文件。
- [ ] 证据能区分窗口未创建/销毁/隐藏/尺寸异常、首帧或门控未达成、目标选择错误、主画布 renderer/presenter 提交失败及成功 API 提交但窗口仍不可见。
- [ ] 已复现根因时，先有失败证据，修补后相同自动检查通过；没有可复现根因时，只交付本问题诊断候选及 Win7 步骤，不宣称已修复。
- [ ] 对改动执行足以验证的完整 `InkeysRepo.sln Debug|ARM64` 构建和适用的 `InkeysHeadlessTests.exe --no-window` 回归；保留旧发布包和现有现场产物。
- [ ] Win7 SP1 + 仅 KB2670838 实机结果逐项记录：启动可见、首笔可见、选择穿透、定格切换/返回后继续可用。尚未执行项写“待用户实测”。
- [ ] PPT 仅确认既有人工作验结果未倒退；C10 旧断言、长路径、UI3 More 矩形重叠和 Release 性能对照仍为后续事项。

## Out of Scope

- 全套 PPT 重构、交换模式全局替换、重新启用 DWM presenter、修改 `.cso`、性能降级，以及 C10 / 长路径 / UI3 More / Release 性能对照专项。
- 未经明确要求的 commit、push 或发布。
