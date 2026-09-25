# PPT 会话、UI3 与可写页合同（2026-09-25）

## 1. Scope / Trigger

修改 native PPT 消费、UI3 位置/缩放/主栏场景、显式退出或 Draw3 页切换时适用。本页取代旧文档中把 `IdtDrawpad/PptImg` 视为生产页加载路径、把控件隐藏视为结束、把 dispatch 视为保存成功的描述。实际生产入口为 `IdtDrawpadFacade` + 单 Draw3 Host；旧键盘转译源码是 vcxproj 的 `None` 项。

## 2. Signatures

- 新独立 IID `D7A63F98-43B8-4BA1-A875-B55A5A04DC3E`：`IPptCOMSessionState.GetSlideShowState(long afterRevision)`、`EndSlideShowIfSession(long expectedShowSessionRevision)`。旧 IPptCOMServer 的 IID、顺序和 v1 descriptor 不变。
- 状态 envelope 恰含 `schemaVersion/stateRevision/showSessionRevision/bindingRevision/lifecycle/pageStatus/descriptor`。生命周期 `Active/Inactive/Unknown`；页状态 `Valid/EndScreen/Unknown`；descriptor 仍由原严格解析器校验。
- `Ppt::PublishSession(uint64_t,bool,HWND)`、`PublishPageState(int,int,uint64_t targetRevision=0)`、`ResetPositions()`；业务回执 `pagePresented(session,target,current,total)`。
- `PublishProductPresentationUiReady(PresentationReadyIdentity)`、`SetProductPresentationInputSuspended(expectedIdentity,bool)`；target/ready 均含 `sessionRevision`。
- `MessageBox::FallbackPolicy.enabled` 默认true，本入口置false；`MessageBox::IsShowing()` 覆盖排队/模态及回退调用期。`Bar::SetEndShowRequestCallback(function<void(uint64_t)>)` 与 `CompleteEndShowRequest(requestId)` 按请求配对。
- `TracePptTiming(stage,session,target,observedQpc=0)`；环境变量 `INKEYS_PPT_TIMING=1` 开启，默认关闭。

## 3. Contracts

### 状态来源、调度与身份

- COM getter 只取 owner 发布的不可变缓存；相同 revision 返回空字符串，不能在 getter 扫描 Office。事件保留原页码读取并唤醒既有 owner；descriptor 刷新优先于慢维护，ROT/绑定/原轮询兼容路线不因快速唤醒重复运行。
- 每场放映有独立 session revision；begin/end/读取候选存在 generation 屏障。旧事件、迟到候选不能复活旧会话。异常和属性缺失属于 Unknown，不凭失败猜测结束；明确 View.State Done 才分类 EndScreen。
- native 新路径在放映期有界16ms观测，未放映100ms；旧 DLL 放映兼容路径保留50ms。16ms 是调度策略，不是实测端到端延迟承诺。必须保留 descriptor/原始页一致性、文稿 key、binding/target/session revision、SlideID/index 校验。
- runtime snapshot 在字段前取得 wait revision；等待必须使用判定前的版本，不能在决定等待后重新取版本。Host 重启不能复用旧 target revision。
- 可见性与生命周期分离；白板覆盖仅隐藏控件并拥有其工作区，不保存位置、不重放 PPT 进入/退出。临时无效页只暂停写入；不同 binding 的未知目标隔离。若已捕获的show HWND确实销毁，或成功查询到其拥有PID已改变，即使COM为Unknown也结束旧会话；不能以暂时隐藏或失焦作同样判断。

### Draw3 页边界

- target 接受后关新 contact admission；绘制线程收尾已接受持久笔迹到旧页，保留 FIFO 已接受命令及保存边界，取消 Laser，停止重连/惯性。未抬起的物理接触隔离至真实 Up/Cancelled，不允许跨页续写，也不无界等待 Up。
- 文档切换和成功 Present 是不同阶段。只有 replay + Present 成功才发布 identity-ready；页码随后带同一 target 发布。可见 PageControl surface 全部成功提交（没有可见控件则直接回执）后，匹配当前 target 才开放新输入。
- Unknown/EndScreen 暂停后清除旧 UI-ready；恢复同一页也须取得新回执。旧 UI ack、load/save 完成和 Host 重启前结果不能重新激活旧目标。
- parked/warm slot 先恢复旧 target，再比较有序 SlideIDs；普通 pageIndex/当前 SlideID/targetRevision 变化不是拓扑变化。

### 位置、保存和有效缩放

- facade 拥有运行偏好；PageControl 候选和成功提交按 session、layout epoch、pair version 交接。全局 direct-move revision 只用于过期帧门禁，不是另一组的提交版本。旧完整快照在 ownership 释放后也不得覆盖新位置。
- 拖动永不触发写盘。开关打开/关闭瞬间保存；记忆开启时真正结束放映或正常退出软件保存；关闭后保持冻结位置。每场进入恢复最近一次明确保存请求的冻结基线（当前进程可含尚未完成的请求），新进程恢复实际磁盘值。重置无论开关均更新默认值。
- 冻结请求基线与 durable saved revision 分开，不能将待保存称为已保存。两个业务队列统一版本仲裁；旧 JSON 跳过。临时文件完整写入/flush 后原子替换；失败保留原文件和原请求重试，不采集后续临时拖动。
- DPI、用户缩放、最终有效缩放分别归一化；最终值传入 Scene 后不重复 DPI 上下限裁剪。绘制、文字、命中、阴影、presentation 使用相同值。主屏布局边界与 DPI 来自同一显示快照；运行适配不写保存偏好，backing capacity 不决定呈现大小。白板继续独立布局。

### 主栏、确认和焦点

- 进入时展开低栏变居中低栏，主体下沿距放映屏幕底边5 DIP；普通展开保持；收起保持位置，原低栏解除约束不跳变。真实退出时展开居中并恢复工作区底边；收起保持。只在 session 边沿应用，白板场景优先。
- 主栏和四分页控件不互为位置障碍；共享光标光效接收区域不属于排斥逻辑。
- 仅主栏 EndShow 点击确认。PageControl 结束图标/滚轮/长按及原生 Esc/Office 退出不新增确认。请求携带 session 和 request id，过期完成不能解除新请求。MessageBox 在业务线程且不持渲染/窗口锁，默认取消、关闭取消、失败关闭，不走系统回退。
- 确认执行时再次校验 managed session。返回1才代表匹配目标的 Exit 调用成功；0为过期/状态不匹配，-1为失败/超时。未实际执行的超时请求不能迟到执行。取消、失败或新会话不改变模式。
- 焦点只在有效进入和明确业务动作后交接，验证 HWND/PID/当前会话/前台；设置、颜色编辑和任何 MessageBox 请求期间不抢焦点。绘制、拖动、渲染和轮询不调用焦点转移。

## 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| remember off 拖动后页码/显隐/设置刷新 | 保留运行位置，保存基线不变 |
| end/toggle 保存未完成立即重入 | 恢复冻结请求基线，durable revision 不提前推进 |
| 文件替换失败或旧 Settings payload 后到 | 保留原字节、当前偏好及对应重试；旧 payload 不回写 |
| DPI2 × user2.5、最终<.5 | Scene、背景、控件、命中和呈现同尺度 |
| 按住笔/鼠标/触摸切页 | 旧页收尾，旧 contact 后续样本不写新页 |
| 画布成功但页码 surface 失败 | 新页输入保持关闭，成功重试后才开放 |
| 同页暂停再恢复 / Host 重启 | 旧 ack 无效，重新呈现/回执，不复用旧 target revision |
| 新场次在旧确认框期间开始 | 旧请求拒绝，不退出新场次、不切新场次模式 |
| 白板覆盖 / 临时 descriptor 失败 | 不触发正常 PPT 退出或位置保存 |
| 无事件且同 HWND 的结束重开完全落在观测间隙 | 外部可观测性限制；必须设备验收，不能宣称仅缓存已证明该提供方全部场景 |

## 5. Good / Base / Bad Cases

- Good：记忆打开→移到顶部→关闭并冻结→临时移到底部→重入恢复顶部；重置关闭态也持久更新默认。
- Base：相同目标和页码 heartbeat 幂等，不重复布局动画/descriptor 解析/磁盘保存。
- Bad：FinishDragPersistence 当成磁盘完成；等 active.empty 才发现换页；取一个更新的 revision 去等待已经发生的 ready；只先显示新数字而仍写旧页。

## 6. Tests Required

- 生产 PositionState/WriteJournal 与真实临时文件失败测试：旧完整快照、交错 pair、session/epoch、freeze desired vs durable saved、失败重试。
- 实际 BarSurfaceScene offscreen 绘制/像素/命中与白板比例测试，不能只验证重复公式。
- ContactInput 并发 Up/retire/admission、Draw3 hidden held-Down→switch→Present→UI ack→old Up→fresh Down、late ack/暂停/Host reset/warm重排。
- managed owner/cache/退出及 acquisition-release ledger；native envelope 复用 descriptor parser，过期会话/请求确认状态机。
- 完整 Debug|ARM64 Solution、headless、hidden/offscreen、托管测试、i18n、diff check。真实 Office/WPS/设备与延迟/CPU 另列实际条件，未跑标 NOT VERIFIED。

## 7. Wrong vs Correct

~~~cpp
// Wrong: 刚发生的 ready 已被这个新基准吞掉，还要再等一个采样周期。
if (!ready) WaitForProductRuntimeRevision(ProductRuntimeSnapshot().runtimeRevision, 50);
// Correct: snapshot 在判断之前捕获，通知发生在任一侧都能保留进展。
const auto before = ProductRuntimeSnapshot();
CheckTargetAndPublishPage();
WaitForProductRuntimeRevision(before.runtimeRevision, timeout);

// Wrong: 隐藏分页就当作真正结束；mem-off 松手也丢弃运行位置。
PublishSession(session, presentationVisible, hwnd);
if (remember) CommitRuntimePositionAndSave();
// Correct: 生命周期独立；提交运行位置与保存触发分离。
PublishSession(session, authoritativeShowActive, hwnd);
CommitVersionedPairPosition(); // 始终；仅 toggle/end/reset 冻结保存请求
~~~
