# 续接交接（2026-11-08）

## 当前结论

用户明确确认当前版本没问题，并授权提交本轮改动，但不结束任务。当前鼠标/Touch手感修复是后续工作的保留基线，不回退到7b逐字Chrome策略。任务继续in_progress，不归档、不push。

已完成：回调级导航历史/Down水位、Stop与overflow隔离、可靠尾段与反向保护、向量摩擦、当前残余投影连甩、跨来源隔离、独立CanvasPan控制台开关。生产合同见 `.trellis/spec/native-desktop/canvas-pan.md`，实施范围见implementation-scope.md，验证证据见validation.md。

用户确认是当前版本的体验反馈，不代表已经完成多种屏幕、不同触屏硬件或所有边界场景验收。

## 后续顺序

1. **先设计并评审设备尺度阶段**：读取repair-plan.md的设备补充及research/display-scale-audit-2026-11-08.md。保留当前默认手感作对照；把物理单位转换与响应参数分开实施，避免一起调估速/摩擦/尺度。
2. **复用已有标尺和来源校验**：DisplayScale、InputSource、monitor/输出映射、旋转、EDID可信度及DIP回退。直接Touch可使用真实物理尺度；鼠标/数位板不能用显示器尺寸冒充动作表面。手势锁存尺度，实际显示输出变化停止旧运动，不重新解释历史坐标。
3. **桌面/大屏独立响应**：沿用笔速橡皮的来源分类和物理长边/显式模式约束思路，平移独立定义释放门限、摩擦、残余增强和上限；不直接搬橡皮尺寸/速度系数。先验证同profile下分辨率/DPI变化不改变响应，再分别标定桌面/大屏。
4. **笔的边界保持**：当前笔hover/落笔仍停止画布，不参与平移。屏幕笔混合响应与外置数位板间接响应需分开设计；若要新增笔平移，先明确产品需求，不能作为尺度适配附带启用。
5. **输入保真后续**：本轮仍RTS每回调批末样本、arrival QPC。探测真实packet timer/metrics、回卷/epoch及批内全样本可用性；有证据再接入，不均分虚构时间、不切换输入后端。此项独立于尺度适配，若当前设备日志稳定，不以它阻塞尺度设计。
6. **分段验证和用户实测**：保留现有鼠标/Touch回归、源切换/短反向/高频量化/overflow保护；新增跨DPI/横竖屏/EDID失败/映射冲突测试。每段确认当前电脑手感不退化，再做大屏对照；未获GUI授权时不自动开窗口。

## 诊断与构建

设置→软件调试→控制台输出→白板平移与惯性诊断；默认关闭，开启重启生效。保留异常前后完整[CanvasPan]日志及输入类型；区分输出队列丢弃和输入历史丢弃。

已通过：ARM64完整solution、完整Headless --no-window、平移专项、control-fence、2112行pan-feel、真实diagnostic-output测试、20语i18n及格式检查。用户本轮已确认当前体验没问题。

本机构建注意：动态vswhere找原生ARM64 MSBuild；同次PowerShell规范Path/PATH并禁node reuse。本轮沙箱多节点启动异常，正常执行环境/m:1可用；曾遇LNK1000 IncrBuildImage，清Build/ARM64/Debug/Inkeys.ilk生成缓存后成功，不改项目/SDK/签名配置。完整构建至少5分钟预算；失败先看第一个有意义错误。

## 交接约束

本轮commit授权仅本次有效，后续任务不得据此继续自动commit。用户未授权push。不要运行finish-work/archive或自动提交日志；下一会话续接此任务，先更新下一阶段scope/design/implement再实施。当前阶段没有已知未解决的自动回归失败。
## 鼠标延迟修复续接（2026-11-08，优先于前述阶段计划）

用户先暂停屏幕适配，反馈Mouse拖动画布延迟、Touch正常；本轮已在CanvasPanMode纯Mouse位置发布处移除多余cursor control wake，保留状态边沿/clear/模式变化与非Pan重绘，并新增生产接线回归及render/Present诊断。实施与证据见mouse-latency-scope.md、mouse-latency-analysis.md、mouse-latency-validation.md及research/mouse-latency-*.md。

ARM64完整solution、完整Headless无窗口回归、control-fence、2112行pan-feel、diagnostic-output和平移专项全部通过。用户现明确确认鼠标正确并要求commit，但不结束任务。只提交本轮修复及相关记录；已有scale-stage规划与相关PRD/design/jsonl/task.json脏改动留存，不混入本次commit。任务继续in_progress，不archive/finish-work、不push；后续阶段等待用户继续指令，本次授权不延续。

## 多设备漫游与惯性设置规划（2026-11-08，当前阶段）

鼠标修复已提交为 961ffdf8；用户要求在同一任务规划添加笔、笔尾、触控板漫游，以及惯性总开关和五类输入细分。默认鼠标/笔尾关闭、其余开启。当前范围/待决见 multi-input-stage-plan.md，输入及设置证据分别见 research/multi-input-roaming.md、research/multi-input-settings.md。屏幕尺度继续延期；不按任务既有 in_progress 状态直接开始实现。任务不结束、不归档，本轮没有新的 commit/push 或 GUI 授权。

用户后续明确确认：拖动/平移模式中全部来源的惯性不再因笔靠近急停，只在真实落笔按下停旧惯性；触控板本轮仅无需按压的双指滑动，按压拖动暂不新增支持。已写回当前阶段 PRD/plan；仍处于规划，待惯性接管技术方案收敛后评审。

当前阶段已完成 PRD convergence：M1–M9/AM1–AM8、design/implement、两份研究和 implement/check 上下文齐备，最终摘要为 multi-input-stage-plan.md。方案提出总开关默认开、热关闭停止/不复活、触控板使用 Win10/11 PTP 系统惯性手感，旧平台保留原降级；这些随最终摘要交用户评审。尚无本阶段实现授权，不因旧任务 in_progress 或用户前次 commit 授权自动开工/提交。后续若用户批准该摘要，直接执行当前清单，不重新询问已确认的双指范围/hover规则。

用户已明确“批准，并开始实施”：本阶段从方案评审进入执行；无需再询问双指范围、hover规则或实现许可。代码按已评审scope分三路实现，主会话负责ARM64完整solution/无窗口验证和整合；GUI、commit/push和结束任务仍未授权。

## 当前阶段交付（多设备漫游与惯性设置）

已按用户批准完成代码，最终 InkeysRepo.sln Debug|ARM64 原生build exit0/126.532s；config-boundary、control-fence、2112行pan-feel、headless-pan、setting-general、diagnostic-output及完整Headless --no-window均exit0（完整575.61s）；20语750键及格式/冻结SHA检查通过。见multi-input-validation.md和research/multi-input-review.md。

功能入口“绘制 → 通用 → 白板拖动惯性”：总开默认开，Mouse/PenTail默认关，其余开；Pen/尾及PTP双指可漫游，移动模式hover不急停，Down停旧coast。PTP目标Win10/11，系统惯性不叠加自有曲线。真实PTP/物理笔/RTS共存和光学体验尚未实测；本轮未GUI/Computer Use。

产物 Build/ARM64/Debug/Inkeys.exe；哈希清单 Build/Logs/multi-input-artifact-hashes.json。首轮LNK1000仅清生成Inkeys.ilk解决，随后缺GMF include、producer Pen入环、Debug Rig栈和旧fixture原点问题均已最小修复并重新完整验证，无当前未解决自动失败。未改工程/SDK/依赖/摩擦参数/屏幕尺度。

任务继续in_progress，不调用finish-work/archive，未commit/push。当前实现授权已完成范围，后续从用户设备反馈或新指示续接；先前commit授权不延续，不自动开始尺度或绘制模式阶段。

## 最新：真机“所有设备不动”回归修复

用户报告前轮构建所有接触设备不动、PTP偶发后，本轮修复DM输入前端：完整INPUT/EXPLICITHITTEST与手动更新配置、整类hit-test不落默认关联、S_OK不伪Running、真实Interaction BEGIN/END与NeedsUpdate独立于held、初始READY不误退候选、固定配置不在input处理中重配；真实BEGIN抓停旧coast但普通设备仍可接手。共6个产品/测试文件增量，UI/config/摩擦/原RTS/尺度/工程未改。

本轮独立证据在multi-input-regression-scope.md、analysis.md、validation.md及research/multi-input-regression-*.md。最终ARM64完整build exit0/112.672s；control-fence13.797s、pan-feel2.719s(2112rows/0fail)、headless-pan2.875s、diagnostic2.313s均exit0。完整Headless首轮exit1/550.938仅未改动Scheduler诊断时序断言；不改任何代码/断言，同一hash完整复核exit0/494.547s。首轮失败保留，具体时序触发原因未确定。

本轮source/二进制hash在Build/Logs/multi-input-regression-artifact-hashes.json，Inkeys.exe为a3eb4d899448630159ec0fcbcd28eb9d022bda6bec9e4b98d75ca94bb6f9b805；请勿混用前轮d02f产物。新frontend函数与策略模型覆盖前轮测试盲区，但没有GUI/Windows实际输入注入，真实PTP/RTS/物理设备仍等待用户现场反馈，不能把模型PASS说成真机恢复。未commit/push/归档，任务保持in_progress。


## 2026-11-12：RTS/DM 同 HWND 根因与 PTP 暂停修复

最新系统证据已取代前述“补 INPUT/EXPLICITHITTEST 即可保证 RTS 共存”的判断。独立探针在本机复现：RTS 先启用后同 HWND DM Activate=0x80070005，反序 RTS Enable=0x80070005；DM 失败后原 RTS Disable 异常。独立 DM HWND 只能解决初始化冲突，不能交付触控板：未注册主窗口时合成 PT_TOUCHPAD 不到达，注册主窗口后跨 HWND SetContact 仍返回 0x80070490。完整证据和原始日志位置见 research/dm-rts-coexistence-probe.md。

用户明确选择“先禁用触控板扩展”。当前产品增量在 `Draw3.WindowControl.cpp`：`kTouchpadDirectManipulationQuarantined=true`，`TouchpadNavigationOwner::Initialize` 在任何窗口写入、COM/CreateViewport/Activate 前返回 false，并记录 quarantine 诊断；既有 frontend production probe 新增防重新启用断言。Mouse/Touch/Pen/PenTail、RTS 后端、绘制、动力学、配置和 UI 不改；PTP 双指及系统惯性暂停，Touchpad 设置仅保留偏好。

已完成最终源码 UTF-8 BOM+CRLF 核对；完整 InkeysRepo.sln Debug|ARM64 两次 exit0（第一次38.493s，恢复BOM后最终11.477s），control-fence、2112行pan-feel、diagnostic-output和Headless --canvas-pan-only均exit0。完整Headless --no-window exit0/494.208s，末行PASS animation correctness，无失败记录。产物 Build/ARM64/Debug/Inkeys.exe SHA256=f12e5853586cd0be65f128b9b0df57d0151fe749fe797daa87e692bc4d0dd339；WindowControl 源码 SHA256=1b535190e51347d2ba23684dd6d6abfca09039c028176ac6b6b292e2d37f2ea2。

任务继续 in_progress，不 commit/push/archive。真实 Mouse/Touch/Pen/PenTail 拖动→回绘制仍需用户使用该新产物验收；独立探针和自动测试不能代替这一现场闭环。


## 2026-11-12：未来触控板路线与本轮提交授权

用户要求保留未来重新实现触控板的可能性，并明确未来兼容评估至少覆盖 Win10。已新增 `research/touchpad-future-options.md`，用统一的 Scope/Signatures/Contracts/Validation/Tests 结构记录：

- 旧方案失败在 RTS 与 DM 同 HWND 的 Activate/Enable 输入委托冲突，不是单纯 viewport flags 或 Running 状态；DM 失败回滚撤销原 RTS 委托是有静态链支持的强推断，私有槽未动态 hook。
- 独立 DM HWND 已被真实合成 PT_TOUCHPAD 否决：主窗口未注册时无输入，注册后跨 HWND SetContact=0x80070490；分窗初始化成功不等于功能可用。
- Win11 候选为主窗口 RegisterTouchpadCapableWindow + 完整 touchpad Pointer history + Interaction Context。它不调用 DM Activate，因此避开本次冲突；但公开最低 Windows 11，且 IC 输出/单位/惯性/产品集成尚未验收。
- 若 Win10 也需要完整 PTP，可研究 DM 与 RTS 的互斥生命周期，并提供拖动期 Mouse/Touch/Pen/PenTail 替代 contact producer；回绘制时完整退休 DM、建立新 RTS 代次。该方案改动大，干净 RTS→DM 正序和真实 Win10 PTP 尚未证明，不得直接解除 quarantine。
- 普通 wheel 可作为兼容降级但不是可靠 PTP；RegisterPointerInputTarget 的 UIAccess/全局重定向约束及私有 HID 路线均不应被当作简单替代。

本轮仍只有 quarantine 产品修复和文档/记录更新，没有实施上述候选。用户本次明确授权为当前改动创建 commit，但任务不结束；不调用 finish-work/archive，不 push。真实 Mouse/Touch/Pen/PenTail 拖动→回绘制仍待用户现场验收。
