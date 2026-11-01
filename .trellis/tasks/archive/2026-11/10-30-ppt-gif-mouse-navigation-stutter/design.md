# 技术设计：PPT 动态 GIF 页翻页卡顿修复

## Context

调查证明主要等待发生在完整 descriptor 被接受之前。当前 `RefreshPresentationDescriptorIfNeeded` 无论普通翻页还是周期维护，都会调用 `PresentationDescriptorReader.ReadShow`，逐页读取全部 `Slides.Item/SlideIndex/SlideID`。该同步工作与 Next/Previous 共用唯一 Office owner，导致动态 GIF 冷态下的 Office 延迟被全文稿 COM 调用次数放大。

推荐方案是 **快速当前页校验 + 按需完整拓扑校验 + 命令优先 + native 等待反馈**。不修改 Office 输入方式，不放宽墨迹 ready 门禁，不改变公开 COM ABI。

## Architecture and Boundaries

### 保持不变的边界

- `IPptCOMServer`、`IPptCOMSessionState` 的 GUID、方法签名和顺序不变。
- `GetSlideShowState` envelope 和 Presentation descriptor JSON schema 不变。
- Office COM 仍只由 `PptComService` owner 访问；getter 继续只返回纯值缓存。
- native 仍要求 raw 页码与 descriptor 匹配，并依次等待 target → Draw3 Present → PageControl ack 后开放输入。
- PageControl 等待反馈只是视觉状态，不是页事务回执，也不改变输入权限。

### 修改边界

- `PptCOM/PresentationDescriptor.cs`：拆分当前页快速读取与完整拓扑读取，复用既有 COM 释放和错误分类。
- `PptCOM/PptCOM.cs`：维护 owner 私有的已验证拓扑缓存，选择快速/完整刷新，调整非必要维护与命令的顺序。
- `PptCOM.Tests/*`：用现有 FakeAccessor/FakeGraph 验证调用次数、失效矩阵、顺序和释放账本。
- `Inkeys/IdtPlug-in.cpp`、`Inkeys/Inkeys/UI/Ppt/*`、`Inkeys/Inkeys/UI/PageControl/*`：发布并显示 native 翻页等待状态。
- `InkeysHeadlessTests/*`：覆盖等待状态的会话、页代次和退休行为。

## Descriptor 双层读取

### 内部拓扑缓存

`PptCOMServer` 在 owner 内保存最近一次 `StableSlideIds` 的纯值快照，至少包含：

- `bindingRevision`；
- 有序 `slideIds` 和总页数；
- 已验证的文稿/窗口元数据，用于构造相同 schema 的 descriptor；
- 有效/失效状态。

公开 `presentationDescriptorCache` 继续按现有锁和 clone 规则发布。内部拓扑缓存只能在完整校验成功后替换；失败、busy 或部分读取不得覆盖最后一次完整快照。`InvalidatePresentationDescriptor`、换 binding 和清理时必须使其失效。

### 快速路径

新增内部快速读取方法，不改变公开 reader API 的使用方式。快速路径只读取：

1. 当前 `View.State`；
2. 当前 `View.Slide` 的同次 `SlideIndex/SlideID`；
3. `Slides.Count`。

沿用每次 acquisition 的具名变量和 `finally` 子到父释放。快速路径可复用内部完整拓扑的条件必须同时满足：

- 同一有效 binding，且内部快照为 `StableSlideIds`；
- 当前不是 EndScreen；
- `Slides.Count` 与缓存一致；
- 当前 `SlideIndex` 合法；
- 当前 `SlideID` 为正，且等于缓存 `slideIds[currentPage - 1]`；
- 读取期间 session/binding generation 未变化。

满足时，以缓存拓扑和本次当前页生成同样的 `StableSlideIds` descriptor，并正常走 `slideShowSession.Publish`。普通翻页和固定周期到期都先走这条快速路径；**时间到期本身不再授权全量扫描**。

### 完整路径

以下情况调用现有完整 `ReadShow`，不得直接复用缓存：

- 没有有效拓扑缓存、binding 改变或显式失效；
- 总页数改变；
- 当前 SlideID 与缓存不匹配；
- 当前页 ID 不可信，需要既有 `PageIndexFallback`/安全降级；
- EndScreen 或其他必须验证完整拓扑的状态；
- 快速读取发现无法安全分类的错误。

完整路径继续执行现有全页顺序、正数、唯一性和当前 ID 校验。只有完整成功才替换内部拓扑缓存。`TransientBusy`、普通失败、部分拓扑和迟到 generation 仍按现有合同处理，不伪造稳定缓存。

### 非当前页重排的取舍

本方案不再用固定 3 秒全扫立即发现所有非当前页重排。其安全性依赖以下规则：

- 非当前页重排不会凭旧缓存开放一个尚未访问的新当前页；访问到 SlideID 不一致的索引时必须先完整校验。
- 若重排不影响当前索引的 SlideID，当前页身份仍正确；后续访问到受影响页时才触发完整校验。
- native 只有在收到新完整 descriptor 后才执行有序拓扑投影；不能把快速路径描述成“已重新验证全部页面”。

这是用“非当前页拓扑发现时机”换取“普通翻页和静止放映不再周期性阻塞”。后续若产品要求立即发现后台重排，应另设计可让出 owner 的增量校验，而不是恢复固定全扫。

## Owner 命令调度

在 `PptComService` 中把已排队的 Next/Previous/Exit 等命令放到非必要 descriptor 维护之前。一次有界命令处理后，再进行必要的观察与快速/完整刷新；在慢维护前再次处理新到命令，避免命令仅因刷新排队而额外等待。

约束：

- 不改变 `SlideShowOwnerMailbox` 的队列上限、2 秒等待和 executing 后不可取消规则。
- 不在渲染线程、getter、事件 handler 或另一工作线程调用 Office。
- 不用无限循环优先命令饿死 descriptor；每轮按既有有界邮箱排水，随后推进一次页面维护。
- 快速路径消除全文稿读取后，剩余的单个 Office 调用仍可能阻塞；调度调整不是虚假的超时保证。

## Native/UI 等待反馈

### 状态来源

新增 native 内部导航等待发布，不进入 COM 或 descriptor JSON：

- 成功把 Previous/Next 放入 `pptUiBusinessCommands` 后，发布 `navigationWaiting=true`，携带 session、方向和递增 epoch。
- outstanding 去重丢弃重复点击时保留原等待状态，不新建 epoch。
- `CompleteRequest`、入队异常、会话结束和 shutdown 清除或替换对应等待状态。
- native COM 调用返回后清除“命令等待”；descriptor/Draw3 的等待继续由现有 `PageStateMode.Pending` 表达。两者可以共同驱动不确定等待指示，但不能互相伪造完成。

发布在释放 `pptUiBusinessMutex` 后进行，不持业务锁调用渲染或窗口服务。

### 分页呈现

`Inkeys.UI.Ppt` 把等待纯值加入现有快照，`PageControl` 在相关方向按钮上显示不确定等待指示（例如旋转/脉冲标记），保留旧页码和原布局。第一版不显示百分比、不提前把页码加一，也不新增 COM 错误码 ABI。

若命令返回但未观察到页变化，等待指示清除，分页仍显示真实旧页并允许重试。已有 `_com_error` 可保留日志；丰富的托管超时原因需要另设兼容接口，不在本修复中伪装成可区分结果。

## Diagnostics

在现有默认关闭的 PPT 诊断通道中增加聚合数值：

- fast/current 读取耗时与结果；
- full topology 读取耗时、页数和结果；
- owner 请求入队、开始、完成、取消/超时及 operation；
- native 等待发布/清除的 session、epoch 和 direction。

不在每个 Slide.Item 上写日志，避免诊断本身放大 Office 调用成本；不记录路径、标题、SlideID 列表或墨迹。既有 `[PptSync]` 阶段保持可供前后对照。

## Compatibility and Rollout

按三个可独立回退的阶段交付：

1. 分段诊断和 managed 调用计数测试；
2. 双层 descriptor 与 owner 调度；
3. native/UI 等待反馈。

公开 COM 接口不变，但 managed 实现改变后仍须独立构建 `PptCOM`，按项目现有交接方式同步同次 DLL/TLB，再构建完整 Solution。不得只替换源码却仍运行旧内嵌 DLL。

## Rejected Alternatives

- **模拟键盘翻页**：只改善 Office 画面，不能解决 descriptor 延迟，还引入焦点、权限、动画语义和错误窗口风险。
- **删除 raw/descriptor 或 UI-ready 门禁**：可能把旧墨迹写到新页，禁止。
- **另开线程并行读取 Office**：违反当前 COM owner 和释放所有权，且 Office 本身仍可能串行阻塞。
- **仅加等待动画**：能改善感知但不减少 COM 工作，不能单独作为修复。
- **恢复更快的固定全扫**：周期阻塞会在 GIF 页反复出现，禁止以缩短间隔换取拓扑新鲜感。

## 2026-10-31 审阅修复设计补充

本轮批准设计见 [审阅修复计划](research/review-fixes-plan.md)：分离新鲜身份与可复用拓扑并隔离放映场次；等待状态记住完成 epoch 以处理乱序；白板失败补呈现绑定原场景/代次/页面并消费时复核。公开 COM ABI/schema、COM owner、输入/真实呈现门禁均保持原合同。

## 2026-11-01 分段诊断设计

用户已批准设计并准备。数值协议、QPC关联、阶段、输出和文件所有权见 research/navigation-perf-plan-20261101.md。跨层不改ABI，COM封送线程改变时只做唯一时间区间关联。

## 2026-11-01 Observe精确定位设计

用户已要求准确定位并执行，dynamic/确定性COM释放为硬约束。先补既有调用子阶段计时与分析展示，人工采样后据证据收窄调度修复；不盲目跳过校验。方案和文件边界见 [observe-substage-plan-20261101.md](research/observe-substage-plan-20261101.md)。AI不操控GUI、不自动提交。

## 2026-11-01 细分实测后的最小ROT修复

用户已批准执行。新数据表明Observe为多个慢COM查询累计；先按 [rot-yield-fix-plan-20261101.md](research/rot-yield-fix-plan-20261101.md) 实施健康binding下ROT安全取消/一次让出公平预算，保留原完整Observe与全部COM释放/墨迹准入，不同时重写观察逻辑。

## 2026-11-01 正常退出超时定位

修复版用户正常退出触发0xE1430016，退出验收不通过。先按 [normal-exit-trace-plan-20261101.md](research/normal-exit-trace-plan-20261101.md) 包裹原清理阶段、保持行为和COM释放；AI不操控GUI、不修改退出策略，不把问题直接归为COM泄漏。

## 2026-11-01 等待标记不可见的边界定位

最新长等待实机三点不可见，静态代码尚不足证明单一根因。按 [navigation-wait-trace-plan-20261101.md](research/navigation-wait-trace-plan-20261101.md) 只补Ppt发布→PageControl合并/配置/绘制/真实呈现的固定数值证据；默认关闭，渲染线程无I/O/分配/等待，停产后flush，不改PptCOM或UI/页码/ready行为。准备验证通过后交用户少量操作采样，不重复同版大矩阵。

## 2026-11-01 用户澄清后的页面准备提示修复

三点实际出现，但随命令Complete提前消失；后续raw/descriptor失配及Draw3/UI ACK阶段仍Pending。按 [page-preparation-wait-fix-plan-20261101.md](research/page-preparation-wait-fix-plan-20261101.md) 独立投影真实Pending为通用准备提示，原命令tracker/水位、COM和输入ready门均不改，不引入soft TTL或猜测完成。

## 2026-11-01 页码动画最小复现与取证

用户同意按 [page-number-animation-repro-plan-20261101.md](research/page-number-animation-repro-plan-20261101.md) 先复现短Status返回Numeric，并准备默认关闭的实际内容策略数值记录；不改变动画决策、不恢复文字替代三点、不改COM/ready，不操作GUI或自动提交。

## 2026-11-01 恢复进度与页码角标

用户已批准准备/恢复进行中使用三点，并指定左右翻页均在页码区域右上角一处显示。按 [page-badge-progress-fix-plan-20261101.md](research/page-badge-progress-fix-plan-20261101.md) 只改显示投影，保留真实故障/重试文字及控制恢复态；Pending/Recovering保持同场稳定显示数字，不能用目标页提前替代。原COM/业务currentPage/ACK/Host门不变，不自动提交。
