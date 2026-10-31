# 实施计划：PPT 动态 GIF 页翻页卡顿修复

> 原修复已提交为 `97e61925`，结果见 [research/validation.md](research/validation.md)。本轮延续三项审阅修复，记录见 [research/review-fixes-validation.md](research/review-fixes-validation.md)；本轮不 commit/push，原实机像素未验项保留。

## 1. 诊断与回归基线

- [x] 在 managed 现有 opt-in 诊断中增加 fast/current、full topology 和 owner command 的聚合计时/计数，默认关闭。
- [x] 扩展 FakeAccessor/FakeGraph 的调用计数，记录 `Slides.Item`、当前 `View.Slide` 和 command/refresh 顺序，不创建平行 COM 框架。
- [x] 保留指定 PPTX 的原始 QPC 基线：冷鼠标最大 descriptor 5.584544 s、input_open 5.657881 s；冷键盘分别 3.963921/4.035565 s。样本仅作对照，不当分位数。
- [x] 增加普通翻页与固定周期维护的 fast-path 测试：同 binding、同页数、当前 ID 匹配时，`Slides.Item` 全量读取次数为 0。
- [x] 增加完整路径触发矩阵：无缓存、换 binding、页数变化、当前 ID 不匹配、EndScreen、显式失效、busy/普通失败、迟到 generation。
- [x] 验证完整成功才替换缓存，失败或部分读取不污染最后一次稳定拓扑；所有 temporary acquisition 恰好释放一次。

## 2. 双层 descriptor 与命令优先级

- [x] 在 `PresentationDescriptorReader` 内抽取共享的当前页读取，保留 `ReadShow` 完整拓扑行为和公开结果 schema。
- [x] 在 `PptCOMServer` 内实现纯值拓扑快照、快速路径准入和按需完整校验；周期时间到期只执行快速校验。
- [x] 处理非当前页重排合同：访问到不一致页时，在完整校验成功前不得发布该页为可信 `StableSlideIds` 目标。
- [x] 调整 `PptComService` 每轮顺序，让有界 owner 命令先于非必要 descriptor 维护；保留队列、超时、会话戳和不可取消语义。
- [x] 增加 owner 顺序测试和反复 fast/full 交错的 acquisition-release ledger 测试。
- [x] 运行 managed 测试，确认原有 `StableSlideIds`、`PageIndexFallback`、`TransientBusy`、EndScreen 和 WPS/PowerPoint fake 差异不回归。

## 3. Native 等待反馈

- [x] 在 `IdtPlug-in.cpp` 成功入队、去重、完成、异常、退场和 shutdown 的对应位置维护带 session/epoch 的导航等待纯值。
- [x] 通过 `Inkeys.UI.Ppt` 快照把状态交给四个 PageControl 客户端；不在业务锁、渲染锁或窗口锁内跨层回调。
- [x] 在 Next/Previous 按钮绘制不确定等待标记，保留真实旧页码、布局和点击语义；不伪造百分比或页事务完成。
- [x] 将等待视觉与 `PageStateMode.Pending` 正确组合：COM 命令等待结束不等于 Draw3 ready，页面 ready 后仍须原有 ack。
- [x] 在现有 headless/PageControl 测试中覆盖重复点击、命令完成未翻页、换场、旧 epoch、退出和 shutdown 清除。

## 4. 构建与自动验证

- [x] 使用本机原生 ARM64 host `MSBuild.exe`，完整构建 `InkeysRepo.sln`，配置 `Debug|ARM64`，超时至少 5 分钟；不单独用 native 项目代替 Solution。
- [x] 独立构建 `PptCOM/PptCOM.csproj Release|AnyCPU` 与 `PptCOM.Tests/PptCOM.Tests.csproj Release|AnyCPU`，运行 `PptCOM.Tests.exe`。
- [x] 按现有产物交接合同同步同次 `PptCOM.dll`/TLB 到 native 使用位置，再核对实际运行 EXE 包含新 managed 实现；接口不变也不得以旧 DLL 做验证。
- [x] 运行 `InkeysHeadlessTests.exe --no-window` 及适用的 PPT/Draw3 hidden 测试。
- [x] 若等待反馈新增文本，运行 `Scripts/i18n.ps1 sync` 后 `Scripts/i18n.ps1 check`；纯图形等待标记不新增文案。
- [x] 运行 `git diff --check`，检查只包含任务需要的源码、测试和明确的产物交接，不提交 Build/Cache 临时输出。

## 5. 真实 Office 验收

- [x] 使用同一指定 PPTX、同一 ARM64 测试产物，开启 `INKEYS_PPT_TIMING=1` 和新增 managed 聚合诊断。
- [x] 冷启动与暖态分别执行至少 5 次第 3→4、第 4→5，覆盖鼠标与键盘；报告 descriptor、input_open、fast/full 次数和最大/中位/长尾，不只保留最好样本。
- [ ] 在 descriptor pending 和 owner outstanding 时连续点击，确认等待标记可见、重复命令仍受控、后续不会永久无响应。重复命令与状态生命周期已由生产/实机路径覆盖；fast 后等待过短，三点标记的真实长阻塞像素截图仍待补验。
- [x] 确认等待期间旧画布不接受新笔迹，旧 contact 不跨页；只有 Draw3 Present + PageControl ack 后才开放新页。
- [x] 验证 SlideID 重排/增删页或对应生产夹具：快速路径不掩盖当前页不匹配，完整校验后拓扑与墨迹槽正确。
- [x] 验证普通无 GIF 页、结束页、Office busy/超时和退出，不出现等待标记残留或命令饿死。
- [x] WPS、其他 Office 版本和 Release 未实际运行时明确标为 `NOT VERIFIED`，不从 fake 测试扩展支持声明。

## Review Gates and Rollback

- 阶段 1 未证明 fast/full 调用计数和释放账本可靠，不进入阶段 2。
- 阶段 2 若出现拓扑身份回归，优先回退双层缓存，保留诊断；不得通过删除 native 身份门修复测试。
- 阶段 3 可独立回退视觉反馈，不影响阶段 2 性能修复。
- 真实 Office 若仍有长尾，依据分段诊断区分单个 Office 调用与剩余全量扫描；不得宣称剩余阻塞已由缓存方案消除。
- 实施后的最终检查通过前，不标记完成、不归档、不提交或推送。

## 6. 2026-10-31 审阅修复（本轮）

- [x] 修复文稿身份新鲜度与跨场拓扑复用，补 managed 行为/释放回归。
- [x] 修复导航完成先到竞态，补乱序/跨场回归。
- [x] 修复白板失败补呈现跨工作区命令，补同场/过期回归。
- [x] managed 构建测试、DLL/TLB 交接、完整 Debug|ARM64 solution 与相关无窗口测试。
- [x] 独立质量审查、最小 diff/BOM/EOL、更新规范与本轮验证记录。
本轮用户未授权 commit/push；留存未提交修改和原实机未验边界。
