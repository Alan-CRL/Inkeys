# 技术设计（2026-10-19 已批准）

## 现状与改动边界
MouseHook仍编译/安装，需要修业务和线程边界，不重新启用旧FloatingHook。当前Hook同步进入工具状态、分页快照和PPT业务队列锁；旧栏内点击取消信号与新pending断链。旧IdtDrawpad.cpp确有键盘Hook→ExMessage→画板HiMsg→KeyboardInteraction→PPT翻页消费者；该文件现为工程None项，活动Facade不安装旧键盘Hook。现行四个PPT HWND bindMessages=false，滚轮由PageControl原生WndProc和全局QueuePptWheel入口转到PPT业务队列。缺少的是当前活动的HiMsg→PPT翻页适配消费者，不是HiMsg不能承载翻页。证据见research两份报告。

三部分共用输入事件、路由身份与Bar收起策略，按单一任务分阶段集成，不拆为可能配置/行为不一致的独立交付。用户已于2026-10-19批准本设计；实际实现和验证见validation.md。

## 1. 专用Hook与有界桥
- 保留std::jthread、消息pump、停止事件，安装/卸载在同线程；先停producer，再销毁消费者目标，不新增常驻线程。
- 负code、未运行和WM_MOUSEMOVE立即CallNext。回调只有限次读原子路由、复制MSLLHOOKSTRUCT必要值、单次有界无分配入队。不调GetStateModeSnapshot、PageControl/PPT业务、HiMsg enqueue、任意std::function、日志格式化或生命周期/业务mutex；不向异步消费者传lParam指针。
- 值型HookEvent包含kind、screen point、message time、delta/button、flags/extraInfo、路由generation及捕获时PPT身份；固定预分配容量，满/停止/路由不稳直接fail-open。只有入口确实接纳的PPT wheel才return1。队列提交是唯一线性化点：提交前失败链传且不得留可见事件；提交后即抑制，可选wake失败也不能再次链传，已有owner poll须能兜底消费。
- 推荐单一消费者为Bar交互owner，在既有等待/轮询入口消费；PPT转发到既有业务队列。沿既有poll/wake，不新增忙轮询；在顶层和Seek、颜色、粗细等嵌套TryRead/Wait入口均有限batch drain，避免Hook积压或计时器/原生/Raw饥饿。若使用窗口通知，必须保证入队/通知失败不会留下已经链传、稍后又执行的幽灵副本；唤醒次数不等于数据量。
- 路由由普通业务上下文发布；所有跨线程字段均安全且读取次数固定，不用潜在带锁atomic shared_ptr、无限seqlock重试或无保护双缓冲。快照不稳立即链传。
- 必要的三个鼠标键缓存改用固定原子位，保留Set/Is API的鼠标语义，键盘map/keyboard hook不改；UI释放不重新依赖缓存。
- 旧100ms业务移到owner，清理本次替代所涉及的未接通mouse-up timer，不恢复RTS Up补发。不把当前无调用者的潜在过期timer当作已证实运行根因。

## 2. PPT wheel唯一准入
旧键盘的消息转译思路可以复用，但不能假定旧KeyboardInteraction仍在运行。本规划推荐异步Hook桥后直接复用现有PPT business queue；若选择HiMsg作为桥的下游，须显式补活动的PPT消费者，并保留来源/会话身份、容量及单次消费合同，不能仅向四个PPT HWND入队。此取舍是现有活动链路与最小改动考虑，不是HiMsg能力限制。原四窗本地WM_MOUSEWHEEL也走同一逻辑准入，但Hook已成功接管的事件由return1抑制原生投递，不再转发第二份。不得以“同毫秒”误去重不同真实刻度。

准入固定在同一份PPT身份/工作区快照，避免先检查PPT、后InvokeDirection重读成白板。事件携带捕获时expected token，消费再核验；该token必须原样进入IdtPlug-in最终business request，不得在QueuePptUiBusinessCommand内重新CaptureCurrent。为此增加必要的native typed ingress/参数，保留最终业务线程核验与COM ABI。只允许PPT命令，不因新场次到来重新认领旧事件，不回落白板callback。

保留现有绘制/擦除模式下全局范围；设置/模态等本应用输入排除状态由owner发布，不在Hook窗口遍历或重型命中。选择/穿透、白板、隐藏/未知放映、失效目标及横向轮不接管。

signed delta净累计到120产生方向意图、保留余数；多刻度按原业务门禁逐次尝试，拒绝意图不无限排队、不跨场重试。模式/路由/会话/工作区变化清余数。保留原Previous/Next outstanding、末页黑页和Draw3页面就绪门禁。EndShow另按token限制为一个pending/in-flight请求，完成/失败/过期/停止释放，避免结束黑页连滚造成无界积压；不改结束确认策略。

本地PageControl滚轮保留原inputLocked/targetVisible和本地工具语义，不能套用全局路线的Selection/ClickThrough排除。累计状态的唯一写者同为Bar交互owner：本地WndProc先做既有准入，再通过独立有界local mailbox投递值型wheel及捕获身份，不能直接修改累计状态，也不能作为第二producer写Hook专用SPSC。local通道可以沿既有线程安全队列但必须有界，Hook绝不接触它的锁。按会话/路由类别隔离余数，单一owner按统一规则处理；本地消息拒绝按原WndProc合同收尾，不伪造已接纳。只有Hook成功接管才抑制该系统事件，避免同事件两路业务。

## 3. Inkeys3独立配置与UI
在INKEYS_CONFIG_SCHEMA的UI.Bar增加AutoCollapse小组，四项IdtAtomic<int>、NoUpload：

| 字段 | 默认 |
| --- | --- |
| UI.Bar.AutoCollapse.Drawing | 1 |
| UI.Bar.AutoCollapse.Erasing | 0 |
| UI.Bar.AutoCollapse.Dragging | 0 |
| UI.Bar.AutoCollapse.OutsideClick | 0 |

0=Off、1=OnlyWhenNotBottomDocked、2=Always。仅JSON整数可表示为int且0..2才合法，缺失/坏类型/超宽/越界各自回默认。沿既有strict codec与reset/ReadMini/copy默认链，不能窄化后判定或重复多套默认。

只读写Inkeys/Config/main.json，不bind旧setlist、不迁移、不双写deploy。旧deploy读写和未编译旧源码暂保留，不顺带退休；活动Hook/Bar/新UI不再拿它们作为策略来源。旧deploy各种组合不能覆盖新默认。

设置页主栏配置附近添加独立“自动收起”块，两种样式始终显示四行相同下拉。直接读新Config，变化才更新和QueueConfigWrite；沿设置FIFO冻结异步保存，不持ImGui锁同步等HWND/磁盘。三语/说明包含居中底栏，按现有字体测量处理长文本与缩放，不照抄旧固定高度卡片。只通过正式i18n sync生成头与快照。

## 4. 统一owner策略与真实触发
纯ShouldAutoCollapse(policy, verticalDockMode)不检查合并/分离。1仅Floating允许，BottomDocked+Free/Centered都拒绝，2不限制dock；消费时仍验证真实触发、工具/生命周期和当前策略。请求幂等Fold，不Toggle、不自动展开，设置变化不即时触发。

- 绘制/擦除：保留drawingActivityChanged(bool)用于副面板/光照；它含quarantined contact且无类型，不能当准确绘制开始。沿现有observer→Host→IdtMain→Bar链加窄accepted-stroke-begin元数据，复用实际runtime tool分类。Drawing明确含画笔、Shape、Laser；Erasing按本笔实际工具，覆盖右键/笔尾覆写。每个接受笔迹一次，多指后到橡皮仍单独判断；hover、拒绝/隔离、尾迹、重放不触发。accepted begin携带workspace/PPT session等生命周期身份，消费时复核，旧场景事件不能折叠新场景；它独立于aggregate activity count，短笔划即使已结束、计数已归零，只要起笔事件身份仍有效就应触发策略，不能照搬旧Started迟到过滤。不改ink算法/输入producer。
- 主按钮拖动：沿现有真实阈值，不照搬旧20px；1在底栏真正脱离Floating后才可收，弹性形变/居中变自由不算脱离；一手势最多一次。
- 受控拖动内fold：Bar交互owner执行本次会话自己的折叠，更新该会话fold基线，并执行面板/dock-center清理与必要呈现barrier。外部fold/模式/来源/桌面失效仍取消，不能全局放宽检查；保留绝对抓取点、代次、Raw补充及单次终态，不当作新Down。
- 栏外点击：Hook只给候选。Bar owner排除主按钮/栏体/属性/颜色/粗细/弹出区，在Selection/ClickThrough下保留100ms可取消确认；期间栏内操作、关闭策略、切工具/dock/工作区让旧候选失效。兼容触摸/笔提升可表示点击意图，但不重投pointer Down、不创建第二笔输入；既有来源签名过滤不变，只能执行一次幂等收起。

## 5. 批准后的最小文件区域
| 区域 | 必要用途 |
| --- | --- |
| Input/MouseHook.cpp/.cppm及小型通道/纯策略 | 固定成本、快照、有界桥、生命周期 |
| Other/Other.Inputs.cppm的鼠标分支 | 必要时保持无锁鼠标缓存兼容，键盘不改 |
| Bar.Initialization/Interaction及必要Main声明 | 单一owner、候选取消与拖动内fold |
| Ppt.cpp/.cppm、PageControl.cpp及必要声明 | 一致身份准入、既有业务队列、delta累计 |
| IdtPlug-in.cpp/.h的native入队封装 | 保留捕获的expected token到最终request，EndShow有界准入；COM接口不变 |
| Other.Config.cppm/.cpp、Setting.cpp、三语i18n及生成物 | 新独立配置、四行下拉 |
| Draw3.DrawingController.cppm/.cpp、Draw3.Host.h/.cpp、IdtMain.cpp | 已接受笔迹类型通知接线，不改原聚合activity |
| 现有HeadlessTests、必要工程源码项 | 生产策略/并发/配置/回归接线 |

如需扩大到算法、COM ABI、权限/焦点或旧配置系统重构，停止扩展并重新规划。

## 6. 验证边界
故意停住consumer或持业务锁时Hook入口仍有限步完成；满队列不吞系统滚轮，退出无旧回调。性能只计本应用逻辑与入队，不把CallNext下游hook耗时归因本进程；聚合记录中位/P95/P99/最大值及拥塞链传，不逐包日志。无窗口基准不能保证真实系统绝无卡顿或UIPI豁免。完整测试矩阵与执行顺序见implement.md。
