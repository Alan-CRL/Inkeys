# PPT 三态画布与 UInk 自动保存恢复

## Goal

在不恢复发布版白板入口的前提下，理顺 Desktop、Whiteboard、Presentation 三态及 PPT 多放映实例的画布生命周期；移除妨碍自然退出的 PPT 警示，确认生产键盘路径不再保留末帧退出拦截，并为 PPT 放映建立按文档聚合的 UInk 自动保存、索引和当前进程内恢复能力。

本任务原基线已于 2026-09-04 批准；2026-09-05 新增的稳定 SlideID 拓扑演进范围已获用户批准并进入实现。

## Background

- 白板功能代码及三态模型继续保留，但发布版暂时隐藏白板入口，Whiteboard 当前不可达。
- Desktop 与 Presentation 即使在切换前已有墨迹，也必须各自保留独立画布；场景切换时自动切换到对应画布，不能相互覆盖或串页。
- 同一 PPT 的当前放映页和已保存历史页保存在同一个 `.uink` 文件中，每个 SlideID 对应一个 Canvas；当前放映只投影当前存在的页面，历史 Canvas 不因 PPT 删除页而丢失。
- 当前阶段允许通过重写完整文件进行覆盖保存，不要求 UInk 追加写入。
- Presentation 文件与索引统一位于共享的 `presentation/` 命名空间，不按 Inkeys 进程拆目录；同一 PPT 文档应持续绑定同一个 `.uink`。本任务的自动恢复仍刻意限定为“本次 Inkeys 进程启动后由该进程创建或更新过的记录”。
- 跨 Inkeys 进程恢复是未来目标；本任务先完成当前进程内稳定 SlideID 的页面重排、插入和删除适配。页面内容冲突、跨进程身份变化及用户提示策略仍延期。

## Confirmed Facts

- `Bridge::Workspace` 已有 Desktop、Whiteboard、Presentation 三态；当前缺口是 Desktop 与 Presentation 共用同一 `InkCanvasCollection`，Whiteboard 才有独立文档。
- 当前生产工程不编译旧 `IdtDrawpad.cpp`，而是编译 `IdtDrawpadFacade.cpp`；产品低级键盘 hook 已为空实现。运行时退出警示来自 `IdtPlug-in.cpp` 的 EndShow 业务路径。
- PptCOM 的末页 `NextSlideShow(bool check)` 保护与退出弹框互相独立，可以原样保留。
- 当前 PPT→Draw3 发布只有 workspace 和零基页序号，Host ready 也只比较页序号；多 PPT 同页切换缺少身份闭环。
- UInk v10 已有完整强类型 reader、覆盖保存事务和 Draw3 单向导出；标准 Presentation workspace 的 Canvas 必须使用真实 SlideID，无法取得 ID 的退化数据应使用 private workspace，当前 exporter/importer 尚不能完成这两类映射。
- PowerPoint 与 WPS 官方对象模型都提供 SlideID；PptCOM 仍可能因 COM 接口损坏而退回纯 dynamic 访问并无法取得 ID。COM 稳定时 SlideID 是页面唯一绑定，任意页面重排、插入和删除都按 ID 重新映射；仅在 ID 不可得时按页序号退化保存，并限制为同进程稳定 source、相同页数的 ordinal 恢复。
- `saveSetting.enable` 当前以通用“画布保存”开关发布到 Draw3，本任务复用该开关同时门控 Desktop 与 PPT 自动保存，不增加第二个设置项。
- 详细源码证据与边界见 `research/code-investigation.md`。

## Requirements

- R1. 移除退出 PPT 放映时的确认/警示弹框，使退出请求不再被产品 UI 二次阻塞。
- R2. PptCOM 内“确认页码后再翻页”的既有保护保持不动。当前生产键盘 hook 已为空实现；未编译的历史 `IdtDrawpad.cpp` 不为本任务做无运行收益的清理。
- R3. 明确 Desktop、Whiteboard、Presentation 三态的画布所有权与切换规则。Desktop 与 Presentation 必须使用独立画布；Whiteboard 即使发布期隐藏，也必须保留独立状态语义，不能通过删除三态模型完成本任务。
- R4. Presentation 画布按 PPT 文档和 SlideID 组织。同一 PPT 的全部当前页及已保存历史页映射到同一个 UInk Document 的 Canvas 集合，不同 PPT 不得互相覆盖；页序变化只改变当前投影顺序，不改变 Canvas 所属 SlideID。
- R5. PPT 当前页内容发生变化后，在离开该页时触发自动保存；未发生变化的页切换不得产生无意义覆盖写入。
- R6. PPT 放映退出时，只要该 PPT 存在未落盘修改就完成最终自动保存，不能再以退出时画布是否有内容作为门控；从未发生过持久化修改的全新空放映不生成无意义文件。已经恢复或保存过的 PPT 即使被清空后立即退出，也必须覆盖保存空 Canvas 状态或等价撤销索引，不能在恢复时复活旧墨迹。
- R7. 自动保存维护共享的 PPT→UInk 索引，并定义稳定的 PPT 文档身份，使同一文档跨 Inkeys 进程使用同一保存目标，同时区分同时或先后放映的不同演示文稿；本任务不开放跨进程自动读取。
- R8. 在当前 Inkeys 进程内，对于已经由本进程自动保存并登记索引的 PPT，再次进入同一放映时加载整个 UInk Canvas 集合，并按当前 SlideID 顺序和当前页切换到对应 Canvas。
- R9. 支持多个正在放映的 PowerPoint/WPS COM 对象直接来回切换，包括不经过 Desktop 的 Presentation A→Presentation B→A；保存、恢复、当前页和画布不得串到错误文档。
- R10. 复用既有 UInk v10 文件规范、覆盖保存事务和 Draw3 文档模型；不另造并行持久化格式。
- R11. Presentation 的请求和 ready 状态必须同时包含文稿身份与 SlideID；不同 PPT 恰好位于同一页码时，不得仅凭页序号提前发布 ready。
- R12. 文件读取、导入和保存失败不得用旧 PPT 或 Desktop 画布兜底；失败场景进入隔离空 Presentation 画布并保留原文件供诊断。
- R13. PptCOM 应优先取得真实 SlideID。稳定模式下页面重排、插入和删除必须按 SlideID 保留已有 Canvas；当前放映只投影当前存在的 SlideID，删除的页面 Canvas 不从 UInk 或索引中静默删除，后续同 ID 页面重新出现时仍可恢复。ID 不可得时使用显式 `PageIndexFallback`，仅在同进程稳定 source、页数未变化时按 ordinal 恢复；process-local 身份仍限 exact binding，退化记录不得被稳定 reader 误认为 SlideID 记录。
- R14. 新增的 PptCOM 页面身份读取必须让 PowerPoint PIA 绑定对象、WPS 和损坏接口都经过同一 pure late-bound/dynamic 访问路径。所有临时 `Slides`、`Slide`、枚举器和属性返回的 COM 对象都要在 `finally` 中按所有权释放；不得 `FinalRelease` 当前绑定字段或调用方仍持有的对象，也不得因单页异常遗留 Office/WPS 后台进程。
- R15. 共享 `presentation/` 写入必须沿用来源 revision、原子替换与索引互斥，发现另一个进程已经修改同一 UInk 时拒绝静默覆盖并保留本进程 dirty 状态。冲突合并、另存冲突副本和面向用户的选择界面延期。

## Acceptance Criteria

- [x] AC1. 从普通页或最后一页退出 PPT 放映均不出现退出警示弹框；当前生产键盘输入路径保持无旧末页拦截，PptCOM 页码确认保护保持原行为。（静态/完整构建证据）
- [ ] AC2. Desktop→Presentation→Desktop 往返后，两边原有墨迹分别恢复，页集合、撤销/重做语义及可见画布不混用。（真实 Controller harness 已实现并编译，但因其使用屏幕外 `WS_VISIBLE`，本轮按无窗口约束未执行）
- [x] AC3. Whiteboard 功能开关关闭时入口仍不可见且状态不可达；重新启用后，其画布所有权定义与 Desktop、Presentation 同级且独立。（静态/纯逻辑证据）
- [x] AC4. PPT 某页有真实内容变化时，切到另一页会把该 PPT 的完整 Canvas 集合覆盖保存到同一个 `.uink`；无变化切页不写盘。（存储测试/Controller 静态调用链）
- [x] AC5. 有未保存修改的 PPT 退出时完成最终覆盖保存，且保存判断不读取 `currentPageHasContent`；从未发生持久化修改的全新空 PPT 不创建 `.uink`；恢复既有画布后执行清空并立即退出，会覆盖原 `.uink`，再次进入时不会恢复已清空内容。（clear mutation、scene-stamped command、空 snapshot 覆盖测试及 final barrier 静态证据）
- [x] AC6. 当前进程内退出并再次进入同一 PPT 放映，会从索引命中的 `.uink` 恢复全部已保存页，并按当前 SlideID 顺序显示当前页对应 Canvas；进程重启后的自动恢复不属于本次验收。（存储/import 测试及 Controller 静态调用链）
- [ ] AC7. 两个不同 PPT 放映直接 A→B→A 切换时，各自页数、当前页、Canvas 集合、dirty 状态、索引和保存目标始终隔离。（Bridge/storage 自动化通过；真实 Controller harness 已编译但未执行）
- [x] AC8. 保存失败、文件损坏、PPT 文档身份暂时不可得或 COM busy 时不破坏现有画布和旧文件，并留下可诊断日志；同一 binding 的短暂 busy 保留最后稳定描述符，新 binding 首次 busy 则隔离。（故障注入/managed fake/native parser 测试）
- [x] AC9. 通过覆盖保存验证“不支持追加写入”不会造成同一 PPT 生成多个互相竞争的当前版本。（单文件覆盖测试）
- [x] AC10. 文档 binding 不可得、读取失败或索引损坏时，不发布其他文稿的 ready 状态，不删除旧文件，并允许当前放映使用新的隔离空画布继续工作；仅 SlideID 不可得时进入 PageIndexFallback，而不是复用旧文稿身份。（disposition/parser/import、主索引损坏后备份恢复及主备同时损坏隔离测试）
- [x] AC11. PowerPoint-like、WPS-like 与损坏 dynamic/SlideID 属性抛异常的 managed fake 测试均能返回完整 SlideID 描述符或明确的 `PageIndexFallback`，且每个临时 COM acquisition 恰好释放；当前绑定对象不会被误 `FinalRelease`。真实 Office/WPS 进程验收仍属 Phase 11。
- [x] AC12. 两个 Inkeys writer 对同一 Presentation 文件发生 revision/foreign-session 冲突时，后写者不覆盖先写者、不错误推进索引，并保留可重试 dirty 状态；本期不弹冲突决策 UI。（writer/foreign/source revision 测试）
- [ ] AC13. PageIndexFallback 文件使用可识别的 private Workspace/extra marker，并按 Canvas.pageIndex 保存；当前进程再次进入同 key/source 且页数相同文稿时按相同页序号恢复，页数变化创建新的隔离画布，process-local 身份限 exact binding；其他进程或稳定 SlideID reader 不会把页号误认为真实 SlideID。（fallback 重入/页数变化/process-local 拒绝测试）
- [ ] AC14. COM 稳定并取得完整 SlideID 拓扑时，任意页面重排、插入和删除均按 SlideID 保留既有 Canvas；插入页创建新空 Canvas，当前不存在的已保存页面继续留在同一 UInk，并保留在 index 的 SlideID 并集里但不参与当前放映显示；再次出现相同 SlideID 时恢复原 Canvas。（稳定拓扑变更测试）
- [ ] AC15. 稳定拓扑在异步加载期间发生重排、插入或删除时，旧 completion 仍能结束对应 pending 状态；当前 target 按最新 SlideID 映射收敛，输入在加载终态后恢复，不得因顺序变化永久抑制落笔。（stale completion/input gate 测试）

## Out of Scope

- 恢复或重新开放发布版白板入口及白板 UI 完善。
- 应用重启后的跨进程自动恢复、页面内容合并、跨进程绑定冲突解决及用户提示 UI。
- UInk 追加写入、增量文件尾或日志式持久化。
- 面向用户的“打开/另存为 UInk”文件选择流程。
- 删除或改写 PptCOM 内既有的“确认页码后再翻页”保护。

## Notes

- 相关既有任务：`09-01-uink-file-persistence`、`09-01-desktop-uink-autosave`。
- 详细源码与外部规范证据见 `research/code-investigation.md`；COM 所有权专项审计见 `research/pptcom-slide-id-ownership.md`。
- 本任务属于复杂跨层任务；技术设计和执行顺序分别见 `design.md`、`implement.md`。本次增量设计待用户批准后实施。
