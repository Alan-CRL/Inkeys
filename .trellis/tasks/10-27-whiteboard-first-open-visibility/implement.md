# 实施计划：已批准的三工作区交接

## 当前门禁

- [x] 用户确认三态=Desktop/Presentation/Whiteboard。
- [x] 任务返回planning，冻结10份产品/测试/资产哈希；3份既有未验证补丁保持原样。
- [x] 用户审核prd.md/design.md并批准实施。
- [x] 已获准进入实现；仍禁止GUI/运行时采样，未授权commit/push。

## 批次1：统一协调与可取消交接

- 在现有StateMonitoring/业务边界建立唯一Committed快照、最新Desired请求和有真实责任的Pending交接，复用现有epoch/owner回执，不另起线程。
- 将白板/主栏/分页独立状态写入收敛为同一结果投影；旧接口保留时只能派生。
- 接纳过渡先关闭新Down；批次1必须将Pending fence真正OR接入Host现有新Down准入，而非只增加协调字段。保留ControlWake/准备/帧/UI ACK/终态排空和源数据/工具，准备目标仍用既有身份。
- 生产state-mode CLI覆盖六个三态切换、同态幂等、快速取消/重入、PPT后台变化、旧回执不提交、无输入主动唤醒。

## 批次2：一次原生场景政策与完整白板背景

- 对照冻结3文件补丁逐块取舍，备份后再改，不覆盖用户资产。
- 场景原生政策统一一个请求，准备owner回执→Render/Host/UI真实ACK→完成owner回执交错，owner不等待渲染；内部保留离场撤全屏→离开样式→恢复band/穿透顺序；普通全局刷新独立。
- 复用Freeze和固定owner链；白板背景/白板region完整rcMonitor，Desktop/PPT及旧定格/放大镜1px不混改。
- 同目标幂等，实际写集恢复，真实失败/旧代际/后到安全覆盖/停机有界收束；Shell仅已有worker最新绝对值。
- 生产Window/CanvasPolicy/背景接缝测试，不能用纯predicate代替实际提交逻辑或宣称整个私有Service动态覆盖。

## 批次3：输入、最小化与UI收尾

- 实际Selection在所有工作区丢弃新绘制Down，RTS MouseLeft/Right、Touch/Pen与Win32 fallback一致；保持正常工具/临时擦除。
- 复核批次1已落地的Host Pending fence，不延后该接线；不把activationAllowed当输入门，不产生第二个独立场景权威。
- Hide/inactive ACK先于minimized暂停；恢复同一页/工具，无需额外笔输入。
- 主栏基线/PPT Switch、分页实际UI-ready、合法BasicControl/Unknown、Settings和故障重试入口保留；仅控制呈现和protected/fatal安全退场不等待不存在的InkReady/已停消费者，仍需真实安全ACK。

## 批次4：独立全范围复审与验证

- 更新实现必需的native/ppt-interop协调合同，所有Trellis现有identity/数据/线程规则逐条核对。
- trellis-check独立全范围审查，修复发现后重新验证相应范围。
- 动态定位ARM64原生MSBuild，规范同invocation Path/PATH；InkeysRepo.sln Debug|ARM64，至少5分钟预算，不改toolchain或工程绕环境问题。
- 现有state-mode、draw3-control-fence CLI、InkeysHeadlessTests --no-window及必要存储/Window政策专项；全9份CSO与旧视觉导出白名单备份/恢复。
- 编码/BOM/CRLF、最小diff和原有未关联WIP检查。
- 没有新的GUI授权则不做GUI；真实任务栏、焦点、各Windows版本结果单独列人工未验收，不能由离屏结果外推。
- 用户未另行要求不commit/push。

## 必须覆盖的故障矩阵

| 维度 | 核心断言 |
| --- | --- |
| Settings/外部程序前台、拒foreground、全局TOP不生效 | 能后台正常完成，UI不循环重置、不周期抢焦点 |
| 目标空Selection、帧无dirty、延后工具/输出/UI-ready | 主动补实际帧，无第一笔唤醒；未完成不接Down |
| Native样式/region/band/owner/显隐失败 | 真实失败收束，只恢复实际写集，主栏可取消/重试 |
| mode=false后半失败及整段重入 | 不重新落回全局排序硬门，不反复重建APPWINDOW |
| minimized→close、restore、快速开关 | 不依赖先恢复，不丢页、旧Hide/Show/聚焦不复活 |
| PPT Load/Save失败、PPT后台结束/换目标 | 保持数据与预算，最新可信目标提交，受保护故障安全退场 |
| 相同HWND新generation、device epoch、旧scene/Frame ACK | 旧操作无新代写权，不用业务epoch替代真实身份 |
| 主栏/分页与Desktop基线 | 同场景版本，PPT↔WB不重拍，最终返Desktop只恢复一次 |

本文件为已批准的执行顺序，实施范围限于白板与三工作区及其直接相关遗留问题。

上述批次只是同一任务的实现顺序；所有必需步骤和最终验证完成前，中间版本不作为最终修复交付或发布。
