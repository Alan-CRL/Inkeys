# Journal - codex (Part 1)

> AI development session journal
> Started: 2026-08-15

---


## Session 1: 修复 Draw3 工具光标与透明度

**Date**: 2026-08-17
**Task**: 修复 Draw3 工具光标与透明度
**Branch**: `draw`

### Summary

修复普通笔、荧光笔和橡皮光标的尺寸、颜色与透明度，统一激光笔直径来源，修正 UI3 透明度显示并补充无窗口回归测试。

### Git Commits

| Hash | Message |
|------|---------|
| `ca8d06e` | (see git log) |

### Status

[OK] **Completed**


## Session 2: 修复绘制属性笔型与激光预览动画

**Date**: 2026-08-20
**Task**: 修复绘制属性笔型与激光预览动画
**Branch**: `draw`

### Summary

完成笔型扩展入口交叉淡化、标注线文案修正、Laser 六阶段预览动画、白芯颜色隔离及芯壳端点圆心一致性；ARM64 完整构建、无窗口测试与人工验证均通过。

### Git Commits

| Hash | Message |
|------|---------|
| `ea277bf2` | (see git log) |
| `ae914033` | (see git log) |
| `cf05a3d` | (see git log) |

### Status

[OK] **Completed**


## Session 3: 清理并归档已完成 Trellis 任务

**Date**: 2026-08-20
**Task**: 清理并归档已完成 Trellis 任务
**Branch**: `draw`

### Summary

提交 08-15 与 08-18 既有归档资料，归档 07-17、08-01、08-12、08-13 和 08-14 unified display；解绑并保留 UI3 Bar bottom dock elastic 为唯一活动任务。

### Git Commits

| Hash | Message |
|------|---------|
| `09082259` | (see git log) |
| `10b9753c` | (see git log) |

### Status

[OK] **Completed**


## Session 4: Preserve pen selection across UI3 tool toggles

**Date**: 2026-08-20
**Task**: Preserve pen selection across UI3 tool toggles
**Branch**: `draw`

### Summary

Separated remembered Laser selection from the active top-level tool, removed pen mutations from draw-attribute toggles, unified UI3 pen selection publishing, and added headless regression coverage.

### Git Commits

| Hash | Message |
|------|---------|
| `75c54c0d` | (see git log) |

### Status

[OK] **Completed**


## Session 5: Fix centered bar expand and collapse animation

**Date**: 2026-08-23
**Task**: Fix centered bar expand and collapse animation
**Branch**: `draw`

### Summary

Stable centered bottom-dock frames now derive the main-button root from animated main-bar geometry before descendant layout; removed correction/rebase state, preserved input mapping, and passed ARM64 Debug build plus headless tests.

### Git Commits

| Hash | Message |
|------|---------|
| `07849596` | (see git log) |

### Status

[OK] **Completed**


## Session 6: 合并白板与 PPT 底部翻页窗口

**Date**: 2026-08-24
**Task**: 合并白板与 PPT 底部翻页窗口
**Branch**: `draw`

### Summary

删除独立 Whiteboard 左右 HWND，让白板与 PPT 复用 PptBottomLeft/PptBottomRight；增加三态 owner 门禁、切换回滚和呈现并发保护，并通过 Debug ARM64 完整构建及无窗口测试。

### Git Commits

| Hash | Message |
|------|---------|
| `be95a7bf` | (see git log) |

### Status

[OK] **Completed**


## Session 7: Restore PPT end show action

**Date**: 2026-08-31
**Task**: Restore PPT end show action
**Branch**: `draw`

### Summary

Restored PageControl end-page Next routing through the shared A2 EndShow dispatcher, kept valid-page Next behavior, prevented EndShow repeat, added regression coverage, and synchronized Trellis cross-layer contracts.

### Git Commits

| Hash | Message |
|------|---------|
| `07b0c208` | (see git log) |

### Status

[OK] **Completed**


## Session 8: 主栏底栏显示续修与回归验证
<!-- trellis-session: v=2 fp=294b1e3e0d8a24f4 -->

**Date**: 2026-09-09
**Task**: 主栏底栏显示续修与回归验证
**Branch**: `draw`

### Summary

延续 08-23 主栏拖动闪动与缩窄残影任务，修复窗口呈现交接、居中缩窄旧像素、隐藏按钮动画批次、水平抓取及过期首次吸附帧，完整 ARM64 构建和无窗口测试通过。

### Main Changes

- UI3 Bar 位图、窗口位置、抓手与吸附屏障保持同一成功呈现事务；稳定居中重排完整擦除，隐藏子按钮加入实际布局批次。
- 补充持久像素、跨线程 resize、同目标按钮轨迹、快速重捕获与过期首次吸附帧的组合回归；更新 Trellis 任务和渲染规范。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 完整 InkeysRepo.sln Debug | ARM64，ARM64 host MSBuild，退出码 0，23.78 秒。
- [OK] 全部 InkeysHeadlessTests.exe --no-window 通过，2.13 秒；git diff --check 及 BOM/UTF-8/CRLF 检查通过。

### Status

[OK] **Completed**

### Next Steps

- 维护者复测果冻进出、PPT 按钮收起与居中吸附；任务保留 in_progress。本轮未启动 GUI，未提交 commit。


## Session 9: 底栏捕获旧帧误确认与果冻起点修复
<!-- trellis-session: v=2 fp=4f8f29af7824b1f5 -->

**Date**: 2026-09-09
**Task**: 底栏捕获旧帧误确认与果冻起点修复
**Branch**: `draw`

### Summary

延续问题 1 三帧闪回复现，修复旧浮动帧冒用新状态序号误确认捕获，以及成功底边播入和作废候选重绘请求；问题 2、3 保持已验收状态。

### Main Changes

- CAS 独占发布写者；渲染仅能更新已消费且未抓取的状态，禁止持握 Free/Dragging 被写回 Stable 或旧帧确认新捕获。
- 捕获底端从上一成功显示像素播入，保留精确初值；取消候选保留 visual demand 与完整脏区至成功。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 完整 InkeysRepo.sln Debug | ARM64：ARM64 host MSBuild，退出码 0，19.36 秒。
- [OK] 全部 InkeysHeadlessTests.exe --no-window 通过，1.89 秒；独立审查、git diff --check、BOM/UTF-8/CRLF 检查通过。

### Status

[OK] **Completed**

### Next Steps

- 用户按同一慢速三帧场景复测问题 1；任务保持 in_progress。本轮未启动 GUI，未提交 commit。


## Session 10: 采用用户认可的底栏基线并结案
<!-- trellis-session: v=2 fp=ef51aaee7565def3 -->

**Date**: 2026-09-10
**Task**: 采用用户认可的底栏基线并结案
**Branch**: `draw`

### Summary

用户对比后选择342990fe为最终结果；draw撤回后续调试输出和额外修复，完成验证、提交及任务归档。

### Main Changes

- 产品源码、测试、工程和spec与codex/bottom-dock-before-trace一致，保留Git历史。

### Git Commits

| Hash | Message |
|------|---------|
| `24efcde4` | revert: restore accepted bottom dock baseline |

### Testing

- [OK] 独立只读复核通过；ARM64 host完整Solution Debug|ARM64 exit0/108.69秒，全部--no-window exit0/2.36秒。

### Status

[OK] **Completed**

### Next Steps

- 本问题已按用户最终验收关闭；不继续调试或追加修复。


## Session 13: Archive completed August and September tasks
<!-- trellis-session: v=2 fp=1b86ed45d2f2f566 -->

**Date**: 2026-09-17
**Task**: Archive completed August and September tasks
**Branch**: `feature/eraser`

### Summary

Archived the completed 08-24 overlay recovery task, all three 09-01 persistence and dialog tasks, and the 09-15 eraser attribute task; retained all other active tasks for later testing or revision.

### Git Commits

| Hash | Message |
|------|---------|
| `43d59ec3` | fix: recover overlay presentation z-order |
| `fafa7009` | feat: add desktop UInk autosave |
| `9266a4ab` | localize fluent message box dialogs |
| `d272f888` | feat: add UInk file persistence |
| `3308faec` | fix: align eraser panel release flip |

### Status

[OK] **Completed**


## Session 14: 收敛桌面定格 Magnification 链路
<!-- trellis-session: v=2 fp=46cb685120bfa803 -->

**Date**: 2026-09-22
**Task**: 收敛桌面定格 Magnification 链路
**Branch**: `bugfix/settingui`

### Summary

保留每次抓帧前动态刷新 Inkeys HWND 排除集合，将桌面定格更新和显示收敛为版本化、可取消且失败贯穿的唯一 Magnifier 协调路径；补充生产共用测试与 native-desktop 合同，并完成 Debug ARM64、Headless 及用户实机验收。

### Git Commits

| Hash | Message |
|------|---------|
| `ba14ddd6` | fix(magnification): refresh exclusion list before capture |
| `7df93730` | fix(magnification): serialize freeze capture requests |

### Status

[OK] **Completed**


## Session 15: UI3 i18n 格式串异常防护
<!-- trellis-session: v=2 fp=b796e07f82d852e2 -->

**Date**: 2026-09-22
**Task**: UI3 i18n 格式串异常防护
**Branch**: `feature/ui3-i18n`

### Summary

核实并修复 PR #214 的 CodeRabbit 建议：为 UI3 粗细与帧率本地化格式化增加 format_error 回退和无窗口回归测试；ARM64 构建、headless 测试与 i18n 检查通过。

### Git Commits

| Hash | Message |
|------|---------|
| `0409256f` | fix(ui3): guard localized format strings |

### Status

[OK] **Completed**


## Session 17: Touch 场景曲线修正
<!-- trellis-session: v=2 fp=c2a632b749a69579 -->

**Date**: 2026-09-23
**Task**: Touch 场景曲线修正
**Branch**: `bugfix/eraser`

### Summary

恢复 speed-eraser-physical-scale，在当前 bugfix/eraser 基线修正可信物理 Touch 大屏过早触顶，补中间场景和诊断；详见 validation-touch-profile-20260923.md。

### Main Changes

- Touch 物理/手动路径按可靠表面长边解析有界场景清扫参数，保留 DIP/经验回退和其他设备响应。
- 更新原任务 PRD/design、input-and-ink 等 spec、上下文清单与本轮 CSV/验证记录。

### Git Commits

| Hash | Message |
|------|---------|
| `5997d23b` | fix(draw3): adapt touch eraser sweep to display scene |

### Testing

- [OK] InkeysRepo.sln Debug|ARM64 完整构建退出 0；InkeysHeadlessTests.exe --no-window 退出 0。
- [FAIL] Inkeys.exe --draw3-eraser-hidden-test 退出 1：DComp/ULW 各两条 Window Service owner 关系断言失败；新增 Touch 场景断言未报失败。

### Status

[IN PROGRESS] 原 Trellis 任务保持 in_progress；本轮自动验证已执行，实机手感与隐藏窗口 owner 断言仍待处理。

### Next Steps

- 在 Surface 和教室设备分别人工验收普通/清扫手感；独立排查隐藏测试的窗口 owner 关系。


## Session 18: 输入与橡皮诊断输出扩展
<!-- trellis-session: v=2 fp=5763b97e26f75543 -->

**Date**: 2026-09-24
**Task**: 输入与橡皮诊断输出扩展
**Branch**: `bugfix/eraser`

### Summary

沿用旧控制台开关，更新三语言名称；新增启动显示/EDID 摘要及限频的设备、坐标、面积、橡皮尺寸快照。完整结果见 validation-console-diagnostics-20260924.md。

### Main Changes

- 保留 TouchArea 持久化键，扩展 Host/Controller 帧级诊断，区分活动分辨率与原始 EDID、可见光标与输入位置、非橡皮无效尺寸。
- 更新原任务 PRD/design、native-desktop spec、上下文清单及三语言翻译。

### Git Commits

| Hash | Message |
|------|---------|
| `cf4a1874` | feat(draw3): expand eraser console diagnostics |

### Testing

- [OK] InkeysRepo.sln Debug|ARM64 构建退出 0；InkeysHeadlessTests.exe --no-window 退出 0；i18n check 330/330。
- [FAIL] Inkeys.exe --draw3-eraser-hidden-test 退出 1：DComp/ULW 各两条 Window Service owner 断言失败；新增诊断断言未报失败。
- [BLOCKED] 远端 SSH 读取时报 `sign_and_send_pubkey: signing failed for RSA "Github SSH" from agent: communication with agent failed`；GitHub 随后拒绝公钥认证。未绕过身份验证或尝试推送。

### Status

[IN PROGRESS] 原任务仍为 in_progress；真实设备 EDID 与用户动作手感待采样，隐藏窗口 owner 失败待单独处理。

### Next Steps

- 专项隐藏测试仍有 DComp/ULW 各两条 Window Service owner 关系失败；本轮新增诊断断言无失败。
- 用户修复 GitHub SSH agent 后，再正常推送 `bugfix/eraser`；不使用强推或替代认证绕过。
- Surface 与教室设备启用该选项并重启，采集真实 EDID 与标注动作的限频快照。


## Session 20: Touch与屏幕笔暖状态短快划增长阻力
<!-- trellis-session: v=2 fp=41284dde87eb8867 -->

**Date**: 2026-09-25
**Task**: Touch与屏幕笔暖状态短快划增长阻力
**Branch**: `bugfix/eraser`

### Summary

在原 speed-eraser-physical-scale 任务中复现旧短快划并局部调整 Touch/ScreenPen 标准以上证据与增长；保留场景曲线和面积响应，验证记录见 validation-warm-burst-20260925.md。

### Main Changes

- Touch/ScreenPen 标准以上增长参数局部调整，面积主导增长保留旧响应；增加证据尺寸上限只读诊断。
- 补暖状态时序、场景/回退、频率、尺寸/灵敏度、折返与隔离快划测试及CSV；更新原任务规格。

### Git Commits

`6aefa84e` fix(draw3): resist short touch and pen eraser swipes

原任务保持 in_progress；用户后续授权提交并推送。

### Testing

- [OK] 生产 SpeedEraser.cpp ARM64 无PDB探针运行通过；96组速率最大差0.591%，20组临界目标最大差0.0775 DIP，面积辅助与旧响应最大差0。
- [OK] 修改的 SpeedEraser.cpp、Host、DrawingController、HiddenWindowTest、headless 测试源分别无PDB语法编译通过。
- [OK] 完整 InkeysRepo.sln Debug|ARM64 在本进程临时追加 /FS 后退出0；原命令的两个 vc143.pdb C1041 另行记录，未改工程配置。
- [OK] 最新 headless --no-window 退出0；暖状态240组频率差0.591%，既有 sample/frame 最坏4.613%，精细1728组0失败。
- [FAIL] 最新 --draw3-eraser-hidden-test 退出1，仅DComp/ULW各两条既有Window Service owner断言；橡皮参数、面积、光标和几何断言未报失败。

### Status

[IN PROGRESS] 原 Trellis 任务保持 in_progress；本机自动验证已执行，真实设备手感与独立窗口 owner 问题仍待处理。

### Next Steps

- 待条件允许时复查无需临时 /FS 的常规本机构建；独立排查隐藏窗口 owner 关系断言。
- Surface与教室设备真人手感及光标/新增几何仍需人工验收。
## Session 16: UI3 偶发卡顿第一批修复与诊断
<!-- trellis-session: v=2 fp=d174486dc527116d -->

**Date**: 2026-09-23
**Task**: UI3 偶发卡顿第一批修复与诊断
**Branch**: `bugfix/animation`

### Summary

完成已批准S1-S4：修正颜色块重复描边目标、过滤无影响光照广播、只读DC释放区域与限频非阻塞诊断。完整Debug ARM64构建及新headless无窗口测试通过；原始偶发故障仍待现场反馈，未GUI、未commit/push。

### Main Changes

- 诊断区分回调/动画推进/呈现尝试/成功、各阶段耗时、遮罩计数、资源几何和错误；保持原动画/退避/容量/质量。

### Git Commits

(No commits - planning session)

### Testing

- [OK] InkeysRepo.sln Debug|ARM64 原生MSBuild完整构建及最终增量构建 exit 0；InkeysHeadlessTests.exe --no-window exit 0。
- [OK] 真实动画模块/脏区算法/光影函数计数探针及独立审查通过，新增C4127已修复。

### Status

[OK] **Completed**

### Next Steps

- 如故障用户仍遇到卡顿，依据现有日志中的[UI3Diag]分段信息判断主导阶段；真实Scene hooks/GUI效果仍待运行反馈。

## Session 19: 触摸橡皮擦光标残留修复与验收
<!-- trellis-session: v=2 fp=0fbc95540715f559 -->

**Date**: 2026-09-25
**Task**: 触摸橡皮擦光标残留修复与验收
**Branch**: `feature/cursor`

### Summary

通过来源诊断定位系统注入 Move 错误接管，统一生产过滤入口并补回归。用户确认人工通过；最终日志 2923 条、7 轮触摸、最多五指，无异常链。

### Main Changes

- 增加 schema=2 光标来源、过滤前后状态及 Raw Input 覆盖诊断。
- 触摸归属下拒绝已确认的系统注入未知设备 Move，保持真实鼠标接管。

### Git Commits

| Hash | Message |
|------|---------|
| `31c48f4e` | fix: prevent system mouse moves from reclaiming touch cursors |
| `428860b5` | fix: trace and filter residual touch eraser cursor |
| `5fdd4a67` | fix: 隐藏触摸橡皮擦后的残留光标 |

### Testing

- [OK] ARM64 原生 MSBuild 完整 Debug|ARM64 Solution 与无窗口回归通过。
- [OK] 日志检查器五组自测通过；最终人工日志检查退出码 0，用户确认修复。Pen/Win7 未由本份日志硬件验证。

### Status

[OK] **Completed**

## Session 21: PPT UI3 与切页事务修复及自动化验证
<!-- trellis-session: v=2 fp=a77f11f7e4817ba4 -->

**Date**: 2026-09-25
**Task**: PPT UI3 与切页事务修复及自动化验证
**Branch**: `bugfix/pptui`

### Summary

已完成位置保存/缩放、可信放映会话、Draw3页边界、主栏场景、仅主栏退出确认和焦点；完整ARM64与headless/managed/hidden真实持久化/offscreen通过，真实Office及设备验收待完成。

### Main Changes

- 按审阅方案创建并启动09-25-ppt-ui3-scene-and-page-sync；修复版本交接、保存冻结与原子写入、会话/输入/UI门禁、场景和确认。

### Git Commits

(No commits - planning session)

### Testing

- [OK] InkeysRepo.sln Debug|ARM64、InkeysHeadlessTests --no-window、PptCOM.Tests、Draw3 hidden含真实保存/冷恢复/重排、Bar/PageControl offscreen、i18n、diff check均exit0。

### Status

[OK] **Completed**

### Next Steps

- 按任务research/manual-validation.md进行真实Office/WPS、键盘/硬件、多屏/任务栏和CPU/端到端延迟验收；当前NOT VERIFIED，任务保留in_progress，不提前归档。


## Session 22: PPT 真退出穿透与独立结束页补充修复
<!-- trellis-session: v=2 fp=44fb2cce845377b6 -->

**Date**: 2026-09-25
**Task**: PPT 真退出穿透与独立结束页补充修复
**Branch**: `bugfix/pptui`

### Summary

修复可信退出后的 Selection/窗口收敛；为真实 EndScreen 建立独立 Draw3/UInk 页并完成跨层回归

### Main Changes

- 可信退出边沿版本化 Selection，Window Service 所属线程按最新 revision 隐藏旧画布并只释放主 Drawpad capture
- EndScreen 使用同文稿附加页、独立 pageGuid/历史和明确 UInk marker，PptCOM 同次读取真实 SlideID 拓扑

### Git Commits

(No commits - planning session)

### Testing

- [OK] InkeysRepo.sln Debug|ARM64、Headless、PptCOM.Tests、UInk 全套、Draw3 hidden、Bar offscreen、i18n、diff check 通过

### Status

[OK] **Completed**

### Next Steps

- 在真实 PowerPoint/WPS 与下层输入记录窗口验收 Esc/按钮退出穿透、State 5 冷启动及实体笔；查看 research/supplement-verification.md


## Session 23: PPT 补充修复提交记录
<!-- trellis-session: v=2 fp=4bf4ee76aa2f7441 -->

**Date**: 2026-09-25
**Task**: PPT 补充修复提交记录
**Branch**: `bugfix/pptui`

### Summary

提交真实退出桌面穿透与独立结束页修复；保持任务进行中，真实 Office/WPS 验收待办

### Main Changes

- 代码、UInk/托管/隐藏窗口回归与 Trellis 任务规范已提交

### Git Commits

| Hash | Message |
|------|---------|
| `adfe7fb2` | fix(ppt): restore desktop input and persist end screen ink |

### Testing

- [OK] 完整 ARM64 Solution、Headless、Managed、UInk、Hidden Host、Offscreen、i18n、diff check 通过

### Status

[OK] **Completed**

### Next Steps

- 推送 bugfix/pptui；继续真实 Office/WPS 与桌面系统命中人工验收


## Session 24: PPT UI3 选择态页墨迹和超大控件回归
<!-- trellis-session: v=2 fp=53ebd2f04256dad0 -->

**Date**: 2026-09-25
**Task**: PPT UI3 选择态页墨迹和超大控件回归
**Branch**: `bugfix/pptui`

### Summary

修复 Host 同布尔值新内容版本丢失，并使 PageControl 四窗使用一致预算和可恢复的呈现/窗口提交

### Main Changes

- Selection A-B-A-空页-EndScreen 翻页的内容版本按完整载荷通知，保留辅助 ULW 穿透
- 四窗共享布局预算、失败阶段诊断、资源与单侧 ULW 重试、真实 PPT 窗口结果读回及屏外隐藏回归

### Git Commits

(No commits - planning session)

### Testing

- [OK] 修前 A 隐藏 Host 与 B 非对称预算 Headless 均预期失败；修后完整 ARM64 Solution、Headless、Draw3 hidden、四窗 hidden、Bar offscreen、i18n、Trellis validate、diff check 通过

### Status

[OK] **Completed**

### Next Steps

- 在真实 PowerPoint/WPS 和用户大缩放显示器上采集四窗阶段日志与系统命中，确认现场单侧消失实际原因；任务保持 in_progress


## Session 25: PPT UI3 回归修复提交记录
<!-- trellis-session: v=2 fp=ef7924ff56d50862 -->

**Date**: 2026-09-26
**Task**: PPT UI3 回归修复提交记录
**Branch**: `bugfix/pptui`

### Summary

提交选择态翻页内容版本修复和大缩放下四控件布局/呈现收敛；任务继续进行中

### Main Changes

- Host 按完整内容状态版本通知，四窗共享资源预算并在单侧失败后安全重试

### Git Commits

| Hash | Message |
|------|---------|
| `910de60f` | fix(ppt): restore selection ink and large page controls |

### Testing

- [OK] 完整 ARM64 Solution、Headless、Draw3 hidden、PageControl hidden、Bar offscreen、i18n、Trellis validate 和 diff check 已通过

### Status

[OK] **Completed**

### Next Steps

- 在真实 PowerPoint/WPS 与用户显示器复核选择态页墨迹、左侧控件可见性及系统输入命中；保持任务 in_progress


## Session 26: PPT UI3 任务收尾
<!-- trellis-session: v=2 fp=d9b587125219ccb3 -->

**Date**: 2026-09-27
**Task**: PPT UI3 任务收尾
**Branch**: `bugfix/pptui`

### Summary

用户接受 PPT UI3 当前结果；审查修复、加载页禁用和批注接管已提交，指定任务归档

### Main Changes

- 仅归档 09-25-ppt-ui3-scene-and-page-sync，其他活跃任务不动

### Git Commits

| Hash | Message |
|------|---------|
| `900ab31f` | fix(ppt): retain settings retry and scope exit handoff |
| `1cd45e57` | fix(ppt): disable loading page and restore annotation takeover |
| `d7b9ec12` | fix(ppt): reject stale drawpad visibility after mode changes |

### Testing

- [OK] 完整 InkeysRepo.sln Debug|ARM64、Headless 与 PptCOM.Tests 已通过；用户确认当前人工验证基本无问题

### Status

[OK] **Completed**

### Next Steps

- 处理 PR #217 与最新 dev 的冲突并验证可合并状态


## Session 27: 笔速橡皮 Canary 前结案
<!-- trellis-session: v=2 fp=9e2c9a741c0da203 -->

**Date**: 2026-09-27
**Task**: 笔速橡皮 Canary 前结案
**Branch**: `bugfix/eraser`

### Summary

用户报告人工验收通过；PR #219 已可合并，教室大屏仍待现场验证。按用户要求在 canary 发布前归档原任务。

### Main Changes

- 保留 Touch/ScreenPen 短快划、回缩许可和面积参考安全恢复，以及 dev 的橡皮指针所有权修复。
- 原任务归档为 completed；PR #219 目标分支修正为 dev，并记录未覆盖的大屏范围。

### Git Commits

| Hash | Message |
|------|---------|
| `5997d23b` | fix(draw3): adapt touch eraser sweep to display scene |
| `cf4a1874` | feat(draw3): expand eraser console diagnostics |
| `6aefa84e` | fix(draw3): resist short touch and pen eraser swipes |
| `c777b8dd` | fix(draw3): prevent unqualified sweeps from blocking eraser shrink |
| `aea346c8` | fix(draw3): recover stable touch area after reference mismatch |
| `22706ee4` | Merge dev into bugfix/eraser |

### Testing

- [OK] InkeysRepo.sln Debug|ARM64、headless --no-window、橡皮专项隐藏窗口测试均退出 0。
- [OK] 完整 Draw3 隐藏测试有 22 条 PPT 结束页/页墨迹断言；CodeRabbit 在线检查仍显示 pending。

### Status

[OK] **Completed**

### Next Steps

- 先发布 canary，再安排教室大屏实机笔速橡皮验收；PPT 隐藏测试失败另行定位。


## Session 28: Win7 输入与实际 ULW 像素调查
<!-- trellis-session: v=2 fp=8b580ca70e8d6131 -->

**Date**: 2026-10-03
**Task**: Win7 输入与实际 ULW 像素调查
**Branch**: `chore/publish`

### Summary

复用 win7-main-canvas；纠正鼠标光标与 RTS contact 的证据关系，补运行期 RTS、实际像素和完整重定向采集，交付 x64 诊断包；Win7 根因与恢复待现场证据。

### Main Changes

- 最小修改 RTS/TransparentPresentation/IdtMain；更新本任务调查、验证记录与输入规范；原现场和 shader 文件保持。

### Git Commits

(No commits - planning session)

### Testing

- [OK] ARM64 Debug 与 x64 Release 完整 solution 含最终控制台修正均退出0；ARM64 Headless、两架构 ULW八项断言、36组合及实际CRT无GUI测试通过。

### Status

[OK] **Completed**

### Next Steps

- Win7 连续书写/橡皮/激光笔复现，回传完整采集目录后按 RTS发布与源/DIB像素分界定位；保持任务in_progress。


## Session 29: Win7 PS2 采集脚本兼容性修正
<!-- trellis-session: v=2 fp=75022820f1e4dfa4 -->

**Date**: 2026-10-03
**Task**: Win7 PS2 采集脚本兼容性修正
**Branch**: `chore/publish`

### Summary

现场附件指出 SHA256Managed.Dispose MethodNotFound；替换为旧 CLR public Clear，仅修正采集脚本，应用尚未启动，RTS/像素调查继续等待完整日志。

### Main Changes

- 单行修正 Collect-Win7.ps1；交付小型修正包并记录兼容性边界。

### Git Commits

(No commits - planning session)

### Testing

- [OK] Parser、默认 provider、显式 SHA256Managed 已知哈希/清理测试通过；无GUI、无构建，Win7 PS2未实测。

### Status

[OK] **Completed**

### Next Steps

- 替换同名PS1后再次通过Collect-Win7.cmd复现，回传完整采集目录。


## Session 30: Win7 完整日志确认输入成功与透明读回
<!-- trellis-session: v=2 fp=d1c4f1fcec379173 -->

**Date**: 2026-10-03
**Task**: Win7 完整日志确认输入成功与透明读回
**Branch**: `chore/publish`

### Summary

候选EXE身份一致；13次RTS contact发布并被controller消费，橡皮visual正常生成，15个ULW源采样全透明；定位至绘制/源纹理读回边界，尚无唯一GPU根因。

### Main Changes

- 仅新增调查记录与任务进度；保留现场文件和现有源码。

### Git Commits

(No commits - planning session)

### Testing

- [OK] 只读日志计数、时间线关联和两路源码审查；未构建或运行GUI。

### Status

[OK] **Completed**

### Next Steps

- 同Win7设备对照known-color清屏读回与正式cursor绘制读回，分辨资源读回和共享shader管线。


## Session 31: Win7 GPU 分界自检与采集候选
<!-- trellis-session: v=2 fp=fbff769d356d3a02 -->

**Date**: 2026-10-03
**Task**: Win7 GPU 分界自检与采集候选
**Branch**: `chore/publish`

### Summary

复用原任务；新增独立 Clear/Cursor 像素对照和限频实际 GPU 绑定/Map 日志，两架构构建与自检、ARM64 Headless、采集模拟及独立审查通过，交付 x64 候选。具体 Win7 GPU 根因仍待现场自检。

### Main Changes

# Win7 GPU 分界诊断交付验证（2026-10-03）

继续原 win7-main-canvas 任务；用户明确要求补调试输出并继续 Win7 实测。上一份完整 Win7 日志证明 mouse 接受、RTS 下/上与 controller 13 次 contact，连续笔/橡皮 ULW staging 源全零；具体 GPU 根因尚未证明。本轮只增加判别能力，未改 shader、输入策略、swapchain 或呈现后端。

## 修改与结果

- HiddenWindowTest.cpp/.h、IdtMain.cpp：早退 `--draw3-renderer-pixel-test`，不加载配置或进入单实例/产品窗口。WARP FL11_0、128×128、永不显示 layered fixture HWND、普通 FLIP_SEQUENTIAL，复用正式 renderer/shader。四项正式 Clear/Cursor 像素检查决定退出码；两项关闭混合只是对照。
- RendererPrimitives.cpp：首个可见光标请求及每秒最多一次 cursor-gpu；资源缺失、Map HRESULT、上传完成、Draw 前后、实际 viewport、RTV/backbuffer canonical identity、VS/PS/CB/SRV 绑定和设备状态。Draw(void) 已发出不代表像素成功，保持正式行为。
- Collect-Win7.ps1：先跑独立 probe、最多 30 秒、仅终止自己创建的超时子进程；保存 probe 输出和退出信息，即使失败仍采正常应用。保留 PS2 public HashAlgorithm.Clear() 修正；脚本不修改配置。
- input-and-ink.md：新增七节分界诊断合同；检查报告见 [独立审查](research/check-gpu-probe-2026-10-03.md)。

## 构建与验证

使用 vswhere 定位 ARM64 原生 MSBuild，完整 InkeysRepo.sln，/m:1 /nr:false，隔离 OutDir/ZhjOutputDir；每个构建允许 900 秒。同一 PowerShell invocation 清理重复 PATH 并设置 MSBUILDDISABLENODEREUSE=1。未启动产品主窗口或交互 GUI。

| 检查 | 结果 |
| --- | --- |
| 完整 solution Debug/ARM64 | 退出 0 |
| 完整 solution Release/x64 | 退出 0 |
| ARM64 renderer-pixel-test | 退出 0；四项正式 + 两项对照 PASS；hidden visible=0 |
| x64 renderer-pixel-test | 退出 0；四项正式 + 两项对照 PASS；hidden visible=0；在 Win11 ARM64 运行 |
| 两架构清屏像素 | 16384 非透明/非零 RGB，BGRA=(64,32,16,128)，clear_mismatch=0 |
| 两架构正式 Cursor | 848 非透明/非零 RGB，中心 BGRA=(128,128,128,128)，角像素零；VS invocation=6/PS=1368 |
| ARM64 Headless --no-window | 退出 0，PASS animation correctness |
| Runtime GPU 输出 | upload-success、before-draw、after-draw-issued；实际目标身份和 shader/CB/SRV match=1 |
| Collector | parser 及成功/非零退出/超时/启动异常四条模拟通过；所有路径继续正常采集；模拟未启动应用 |
| 格式/边界 | git diff --check 通过；生产 UTF8/CRLF 且保持各文件原 BOM 状态、脚本 ASCII/CRLF；原 .gitignore 与八份保留 cso 哈希保持 |
| 包完整性 | ZIP testzip 通过，包内 EXE SHA 与构建一致；PE 0x8664；CLI/cursor-gpu/PixelTest 字符串存在 |
| Win7 GPU / PS2 / 可见恢复 | 待同一问题设备实测，不能用本机结果代替 |

已有第三方/IdtMain 数值转换警告、Debug LNK4075 保留；未修改项目或工具链。Pipeline query HRESULT 仅辅助诊断；四项正式检查通过不能掩盖真实产品帧问题。

## 候选身份与实测步骤

源码基线 d03713bdf456c9da80adb6e0df0f27ebd1399765，包含未提交诊断增量；manifest 精确记录六份源码和包内文件 SHA。EXE 43823616 字节，SHA256 `06CD58C7E1E1F01042D489902457C18FF3E32DAF843197386AE7BB7B283DF8AE`；ZIP SHA256 `4D4F3E9B868DECEA8909E8AC6DBF740B3606678C03DF52A85FD75F97B474774E`。

[新 x64 测试包](C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-gpu-probe/Inkeys-Win7-Gpu-Probe-x64-Release.zip)。保留上轮包，未覆盖 G: 现场文件。

1. 退出现有 Inkeys、备份旧 EXE。将本包 EXE、PptCOM 三份运行文件和两份 Collect 脚本放入原测试目录，沿用原配置。
2. 开启 ConsoleOutput.Draw3/ConsoleOutput.Cursor，双击 Collect-Win7.cmd；自动自检后进入应用。
3. 空白页中部鼠标连续画线至少 3 秒；橡皮连续移动至少 3 秒；激光笔连续移动/按下至少 3 秒，记录分别是否显示。
4. 正常退出，回传整个 Win7-Diagnostics/时间戳目录，包含两份 pixel-test、两份 console、identity 和本次 idt log。

Clear 与正式 Draw 的对比会定位设备/读回、生产管线或产品实时绑定；关闭混合对照不能直接作为正式修复。任务保持 in_progress；没有 commit/push/归档。


### Git Commits

(No commits - planning session)

### Status

[OK] **Completed**

### Next Steps

- 用户在原 Win7 环境运行新 Collect-Win7.cmd，回传完整 pixel-test 和正常应用日志，保持任务 in_progress。
