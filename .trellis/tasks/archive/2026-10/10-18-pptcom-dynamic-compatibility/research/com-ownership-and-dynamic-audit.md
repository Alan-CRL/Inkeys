# Research: PptCOM dynamic 与 COM 引用所有权审计

- Query: 对 `PptCOM.cs`、`PresentationDescriptor.cs`、`SlideShowSession.cs` 的 COM 取得、所有权转移、释放和清理并发做只读审计；规划 dynamic 优先、事件失败走轮询、引用及时配平及 PowerPoint/wpp 退出验证。
- Scope: mixed
- Date: 2026-10-18
- Active task: `.trellis/tasks/10-18-pptcom-dynamic-compatibility`
- Boundary: 本文件仅为调查及待批准方案；没有修改产品代码/规范/配置/产物，没有构建、执行测试、启动 GUI 或激活 Office，没有执行 git 操作。

## Findings

### 1. Files found

| 路径 | 一行说明 |
| --- | --- |
| `.trellis/workflow.md` | 区分规划与实施；用户批准前保持 planning，研究持久化到当前任务。 |
| `.trellis/spec/ppt-interop/index.md` | interop 边界、支持声明证据等级及构建入口。 |
| `.trellis/spec/ppt-interop/com-contract.md` | descriptor、COM ownership、cleanup 现行合同；其中反射优先、GC/final release 段落需要按本次用户要求修订。 |
| `.trellis/spec/ppt-interop/native-session-ui3.md` | owner mailbox、会话退出、批注接管与缓存 getter 合同。 |
| `PptCOM/PptCOM.cs` | ROT、绑定/事件/轮询、根对象清理、公共命令、批注、旧页码读取。 |
| `PptCOM/PresentationDescriptor.cs` | 常规属性反射 accessor、特殊 typed HWND、descriptor 临时对象释放。 |
| `PptCOM/SlideShowSession.cs` | 纯值会话缓存、退出 mailbox、按会话执行 Exit 并释放临时 View。 |
| `PptCOM.Tests/Program.cs` | reader 的 FakeAccessor acquisition/release ledger；当前未覆盖 ROT/旧命令/真实 RCW。 |
| `PptCOM.Tests/SlideShowSessionTests.cs` | session、退出与批注 fake 测试；批注 fake 不是 COM 对象。 |
| `Inkeys/IdtPlug-in.cpp` | service thread 与业务调用并存，说明 root cleanup 需要跨线程边界。 |
| `.trellis/tasks/09-04-ppt-canvas-uink-lifecycle/research/pptcom-slide-id-ownership.md` | 历史研究已指出 identity 不等于 ownership、旧比较/轮询与清理竞态；部分建议随后被反射优先合同覆盖。 |
| `.trellis/tasks/09-04-ppt-canvas-uink-lifecycle/design.md` | 旧设计第 137 行认可每次 COM 返回独立配平，第 140 行却固定反射。 |

### 2. 本次用户要求与判断原则

本次用户明确要求：常规 Office 对象访问优先使用 C# `dynamic`；只有该能力经证据确认不能通过 dynamic 实现时，才允许局部使用类型包装/其他适配，事件绑定是典型例外。事件接口失败不能阻断动态访问和轮询。所有本模块取得的 COM 引用必须及时配平，不能依赖 GC 才让 Office 退出。

需要区分三件事：

1. **一次 COM 返回/入站映射**：ROT `GetObject`、对象值属性/方法返回等，是需要记账的边界。重复返回同一 identity/同一 RCW，并不表示没有新增一笔取得。
2. **托管别名**：`dynamic value = existingObject`、赋给列表/字段、参数传递、`as`/cast 本身不新增一笔 COM acquisition，不能因变量增加就多 Release。
3. **所有权转移**：把已经取得的引用交给长期 root 槽，可以让原临时槽不 Release，但必须明确转移、清空临时槽，且替换旧 root 前配平其原持有。identity 比较用于目标身份，不能决定是否释放一笔新取得。

Microsoft 的 RCW 文档说明，同一 COM 对象可被多个托管引用共享；`ReleaseComObject` 文档说明 COM 指针重复进入 CLR 时 RCW 计数会增加，释放时还需防止其他线程正在调用。此处采用“按取得边界记账”，不尝试从托管变量数或 Win32 COM identity 推测 RCW 内部计数。[RCW](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/runtime-callable-wrapper)、[ReleaseComObject](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.releasecomobject?view=netframework-4.8.1)

`dynamic` 不是让所有 COM 故障都恢复的手段：如果 IDispatch/对象创建本身不可用，不能保证联动；其价值是避免把 PIA/类型接口失效扩大成常规读写全部失效。具体旧版 Office/WPS 功能矩阵由本任务其他研究记录；本文件不作版本支持宣称。

### 3. 已有充分源码证据的所有权缺口

以下“确认”指代码路径中没有配平或对同一笔取得重复释放，**不等于已在用户设备测得后台进程卡死或证明它是每次间歇失效的唯一根因**。

#### 3.1 ROT 返回的 bestApp 在“已绑定同一应用”时被漏掉

- `GetAnyActivePowerPoint` 从 ROT 取得对象：`PptCOM/PptCOM.cs:997`；应用 moniker 的返回从 `comObject` 转到 `candidateApp` 并置空原槽：`:1002`；文稿 moniker 则额外取得 `.Application`：`:1011`。
- 函数将获胜候选的同一笔取得转给 `bestApp`：`:1135`，返回前去重列表跳过最终 best：`:1180`。因此调用方拥有这笔返回，无论其 identity 是否等于长期 `pptApplication`。
- 调用方不重绑分支 `:1366` 仅在 identity 不同才释放；已绑定应用通常每轮扫描仍返回相同 identity，于是本次返回没有释放也没有转移。
- **结论：明确漏配平。** 不能以 `FullCleanup(true)` 最终清空 RCW 计数作为维护循环允许积累的理由。

最小改法：调用方对 `bestApp` 使用明确的临时 owner/finally；只有真正转移给 `pptApplication` 时置空。没有转移的返回一律单次释放，不依赖 identity。

#### 3.2 ROT best 候选被替换时，同一笔取得有两处释放

当前顺序：

```text
ROT取得 A -> foundAppObjects.Add(A)                 :1039
bestApp = A，candidateApp = null                    :1135
后续 B 优先级更高 -> SafeRelease(bestApp即A)        :1132
A仍保留在 foundAppObjects
finally遍历列表，A不是最终B -> SafeRelease(A)        :1183
```

`foundAppObjects.Add` 和 `bestApp = candidateApp` 都只是别名，没有第二笔取得。因此 A 被替换时的第一个 Release 与列表清理中的第二个 Release 重复消费同一笔取得。`SafeRelease` 吞异常不能撤销第一次过度释放的影响；如果 RCW 还有其他持有，第二次减计数还可能消耗长期 root 对应的份额。列表中提前释放的 A 也会继续参与 `:1026` identity 比较。

**结论：明确重复释放路径；实际触发依赖同一轮扫描有更优候选替换旧 best。** 与同应用每轮漏释放可以同时存在，两种错误互相掩盖并不构成正确所有权。

最小改法：本函数的去重列表在扫描期独占持有每个唯一候选的一笔取得；`bestApp` 仅为列表内借用选择。更换 best 不释放旧项；最终只把获胜项的那一笔转移给返回值，其余列表项各释放一次。重复 moniker 产生的另一笔取得仍在本轮 finally 释放，不能因 identity 重复就忘记它。

#### 3.3 比较 helper 把 identity 相同误当“没有取得”

| 取得位置 | 当前释放条件 | 缺口 |
| --- | --- | --- |
| `PptCOM.cs:611` `ActivePresentation` | `:624` 与长期 presentation 不同才释放 | 同 identity 的临时取得漏配平。 |
| `PptCOM.cs:638` `ActivePresentation` | `:660` identity 不同才释放 | 同上。 |
| `PptCOM.cs:641` `SlideShowWindow` | `:654` identity 不同才释放 | 同 identity 的临时取得漏配平。 |

这三个取得从未转给 root，finally 应无条件对本次非空 COM 返回单次释放，window 在 presentation 前。借用入参和用于比较的长期字段均不因该 helper 增加释放。

历史研究已在上述旧函数中记录相同问题，本轮不使用 git，不能独立确定其首次引入提交；父会话的历史核对负责区分 0813 前后。

#### 3.4 轮询检测窗口：只有实际转移时才能跳过临时释放

- `:1419` 取得 `ActivePresentation`，`:1463` 已无条件释放，形状正确。
- `:1428` 又取得 `SlideShowWindow`。
- 旧 root 无效且新窗口不同的分支，`:1434` 释放旧 root，`:1436` 把本次临时窗口转给 root；这一条可以不再释放本次取得，但应该将临时槽置空表明转移。
- 其余情况下本次窗口没有转移，`:1466` 却按 identity 相同跳过释放。这是新取得的窗口，即使等于 root 也应释放一次。
- 跳过后的局部槽又可能在 `:1489` 被下一次属性返回覆盖，`:1510` 只释放最后一笔。若未进入该轮询分支，则作用域结束时丢失前一笔显式配平机会。

**结论：明确漏配平。** 修复应使用“转移后局部置空，finally 释放剩余局部”，不能仅把所有等 identity 分支都强行释放而破坏 `:1436` 的真正转移。

#### 3.5 旧业务命令有链式 COM temporary，无显式释放

| 方法 | 代码位置 | 需持有并在 finally 单次释放的对象 |
| --- | --- | --- |
| `NextSlideShow` | `PptCOM.cs:1988`、`:2004`、`:2011` | 每次执行分支取得的 `View`。 |
| `PreviousSlideShow` | `:2022` | `View`。 |
| 旧 `EndSlideShow` | `:2034` | `View`。 |
| `ViewSlideShow` | `:2044` | `SlideNavigation`。 |

这些链式属性返回没有进入受控局部变量；GC 之前无法保证及时配平。改为 `view = root.View; view.Next()` 等显式形状，并在 finally 释放，本身不改变命令语义。没有必要为了修复释放顺便重写末页 polling 规则。

`ActivateSildeShowWindow` 直接在借用 root 上执行 `Activate()`，正常 void 返回不另取得临时对象；它主要需要解决并发所有权，而非添加一个不存在的 Release。

#### 3.6 Begin 事件直接覆盖 root，旧持有未显式交接

`SlideShowBegin(object WnObj)` 在 `PptCOM.cs:862` 直接 `pptSlideShowWindow = WnObj`。此前 root 可已经来自绑定时的 `:1265` 或轮询转移 `:1436`。覆盖前没有释放该旧持有，也没有标记/协调 callback receipt 与 root 的所有权。

**确认的事实**是旧 root 槽的持有没有在这条覆盖路径显式交接；`WnObj` 的赋值本身不产生一笔独立取得，不能把两个变量各 Release 一次。若新旧对象不同，旧 RCW 可直接变成等待 GC 的对象；若相同，重复入站或既有 root 仍需要分别按来源核对。

**需要验证的部分**是生成 PIA 事件 sink 传入托管 handler 的参数究竟由哪个边界拥有。不能把 native `[in]` 参数的借用规则与 CLR 入站 RCW 映射混为一谈，也不能凭 `object WnObj` 的 C# 参数形式宣称已取得或无需记账。本仓旧研究把 event 参数列为 callback 内借用，却允许现代码长期保存它，两者不一致。见下文事件边界方案。

### 4. 部分事件绑定/解绑的确定性清理缺口

`PptCOM.cs:1315`、`:1316`、`:1317` 按顺序绑定三个放映事件；如果第二或第三个 add 抛错，前面成功的 add 没有回滚，catch 在 `:1343` 设置 `bindingEvents=false`。随后 `UnbindEvents` 的 `:352` 以该 bool 为入口，因而不会尝试解除已成功的部分绑定。

`UnbindEvents` (`:360`) 与 `PresentationBeforeClose` (`:920`) 又把多个 remove 放在同一 try 内；第一个 remove 失败会跳过后续 remove，但最后仍把全局 `bindingEvents` 清零。sink 是否因此长期保留 service/root、重复回调或阻碍退出，需要真实 COM 验证；**“成功订阅集合与一个 bool 不对应、异常后未回收”是源码可证实的缺口**。

建议最小修复：逐个记录订阅成功标志/位图；后续 add 失败时逐个 best effort remove 已成功项，并始终启用轮询；unbind 每一项独立异常边界，不能一个失败阻止其余解绑。类型 cast 只是借用别名，不为 `app` 额外 Release。事件包装只限连接点能力，不把普通属性/命令移入包装器。

### 5. 已经正确配平的路径与有限风险

#### 5.1 Descriptor reader

- `PresentationDescriptor.cs:304` 取得 `View`，`:321` 取得当前 `Slide`，finally `:343`、`:344` 先 slide 后 view。
- `:350` 取得 `Slides`，每轮 `:376` 取得 `Item`，每轮 finally `:386` 释放 item，最外 finally `:422` 释放 slides。
- `GetScalar` (`:179`) 拒绝并释放意外返回的 COM 对象 (`:191`)；缓存只放纯值 DTO/数组，没有保存 RCW。
- `application`、`presentation`、`window` 入参是借用，reader 不释放；typed HWND 的 cast (`:85`) 是借用别名，不增加 Release。

**结论：reader 主临时对象的释放形状可保留。** 将常规分派改成 dynamic 不应破坏这些 finally，也不应为 typed→dynamic 两次读取临时对象建立难以核对的双重获取分支。

#### 5.2 原页码 helper

`GetCurrentSlideIndex` (`PptCOM.cs:1908`) 对 View/Slide 逐层取得、finally 子到父释放；`GetTotalSlideIndex` (`:1928`) 对 Slides 释放；`GetSlideShowWindowsCount` (`:1942`) 对 SlideShowWindows 集合释放。它们是本次 dynamic 风格模板。

#### 5.3 Session exit

`SlideShowSessionExit.Execute` (`SlideShowSession.cs:335`) 借用传入 window；`:350` 取得 View，`:359` 释放；意外 COM-valued HWND 在 `:360` 释放。调用前后有 session/generation 检查，不能为了兼容直接删除这类身份保护。`InvokeMethod` 当前正常 Exit 为 void；未来若包装可返回对象的方法，现通用 void accessor 不能静默丢弃返回的 RCW。

#### 5.4 Annotation

`RefreshSlideShowAnnotationTool` 的 `PptCOM.cs:1700` 和 `ExitSlideShowAnnotationToolOnOwner` 的 `:1765` 均取得 View，`:1705`、`:1782` finally 单次释放。公开 getter 只读纯值缓存。

常规属性访问次序仍不符合本次需求：`ReadSlideShowAnnotationTool` 在 `:1673` 优先 typed `PointerType`，`ResetSlideShowPointer` 在 `:1737` 优先 typed set；laser/pointer 的其他分支虽然先 dynamic，失败后又无分类地反射。`TryGetDynamicProperty`/`TrySetDynamicProperty` (`:452`、`:468`) 实际都是反射，名字不等于实现。

正常 `LaserPointerEnabled`、`PointerType` 都是标量，因此不要求 COM release；但当前 object 槽未像 reader 一样拒绝/释放畸形提供方返回的 RCW (`:1662`、`:1679`)。这是防御性风险，尚无证据表明受支持 Office/WPS 会返回这种错误类型，不应把它当真实后台卡死已复现。若本次统一 scalar helper，可在同一改动中按边界处理，不为所有标量无差别调用 Release。

#### 5.5 IUnknown identity 比较与辅助清理

`AreComObjectsEqual` 在 `PptCOM.cs:573`、`:574` 用 `GetIUnknownForObject` 取得两个原生指针，finally `:580`、`:581` 分别 `Marshal.Release`，没有明显配平缺口。不要用 `ReleaseComObject` 代替这些指针 Release，也不要因比较而释放入参 RCW。

ROT 的 `rot`、`enumMoniker`、逐轮 `moniker`、`bindCtx` 都有释放出口 (`:1188`、`:1189`、`:1162`)。`CleanUpLoopObjects` (`:316`) 的三次原始 Release 放在同一个无保护函数内：若前一个对象已因重复释放等原因失效并抛错，后续对象跳过清理。属于异常清理稳健性风险；修复上述主缺口时应让每个释放独立尝试，不能把异常吞掉当作账本已配平。

### 6. dynamic 分派最小改动边界

| 区域 | 当前形状 | 规划要求 |
| --- | --- | --- |
| `MarshalLateBoundComAccessor` `PresentationDescriptor.cs:89` | 所有名字经 `InvokeMember` | 保留可注入 reader seam，在生产实现中对已知成员编译实际 dynamic 调用；不能只重命名 accessor。 |
| ROT 文稿→Application `PptCOM.cs:1011` | 反射 | 单次 dynamic `Application`，独立记账，finally 与 ROT 文稿各配平。 |
| 比较函数 `:595`、`:611`、`:638`、`:641` | 反射 | dynamic 标量/对象访问；保留同文稿比较逻辑，修复 ownership。 |
| HWND `PresentationDescriptor.cs:82`；`PptCOM.cs:1784` | typed 优先/typed-only | 先检验实际 dynamic HWND。只有特定能力已确认不暴露给 dynamic，才保留局部 typed/Win32 兜底；busy 不能算“不支持”。窗口身份安全仍由同文稿/进程/会话条件保证。 |
| 批注 `PptCOM.cs:1658`、`:1732` | typed、dynamic、反射混合 | dynamic 优先；可选 laser 缺失只影响 laser 功能；不要借由属性访问失败触发全局解绑。 |
| 事件 `:1311`、`:356` | typed connection point | 可保留局部包装；绑定失败或部分失败后完整回滚，轮询保底。 |
| 命令链 `:1988` 等 | dynamic 但临时对象未显式持有 | 明确局部、finally、owner 执行；这部分问题不是 dynamic 本身。 |

不要把 dynamic 的名称限制误当必须使用反射：现 accessor 调用点的属性/方法集合有限，可用受控成员分支保持测试注入，分支内部 `((dynamic)target).View` 等由编译器生成真正动态调用。未知名字应该明确拒绝，不能偷偷退回通用反射。这个方案是否沿用现接口名是实现细节，不需要改变对外 COM ABI。

### 7. FullCleanup、public 方法和 COM 回调的最小并发边界

#### 7.1 当前可确认状态

`FullCleanup` (`PptCOM.cs:380`) 先使 descriptor 失效，再 Unbind，再释放 roots；普通清理每个 root Release 一次，退出时 `:395`-`:397` 直接 FinalRelease；随后 `:419`-`:421` 强制 GC。没有明确的 managed 在途调用屏障。

`InvalidatePresentationDescriptor` 的 revision 只能阻止发布旧纯值，不能阻止另一线程继续使用已取出的 root RCW。native `Inkeys/IdtPlug-in.cpp:986` 启动 `GetPptState` service 线程，`:997` 业务队列仍调用 `:400` Next、`:415` Previous、`:431` End、`:447` View；因此“native 服务快照受 mutex 保护”不等于 root COM 图有统一 owner。

`SlideNameIndex` (`PptCOM.cs:1625`)、`GetPptHwnd` (`:1643`)、Next/Previous/旧 End/View/Activate (`:1959` 起) 目前直接访问 live roots。Begin callback 也能更换 root。**存在真实并发访问的代码条件；是否已产生 AccessViolation/InvalidComObjectException 或进程残留，本轮未运行，标为风险而非已复现事故。**

#### 7.2 推荐：延伸既有 owner/mailbox，不重写公寓模型

1. 保持现有 `PptComService` 线程为唯一 Office root 图 owner，沿用已有 `SlideShowOwnerMailbox` 的唤醒和队列；不另建 STA、改 COM 初始化或重做线程架构。
2. 将 `NextSlideShow`、`PreviousSlideShow`、旧 `EndSlideShow`、`ViewSlideShow`、`ActivateSildeShowWindow` 的实际 COM 执行放到 owner 消费的命令中，public 方法只入队并按既有签名返回。同步依赖必须保留有界等待，不能从异步接受冒充已执行。
3. `SlideNameIndex` 和 `GetPptHwnd` 由 owner 发布与 binding/window stamp 配对的纯值缓存，公开 getter 仅读缓存；若确需同步 fresh query，也通过同一个 owner 查询请求，不能在调用线程临时访问 root。保留两个旧 API 的原返回格式/类型。
4. descriptor、session exit、annotation 已在 owner 读取的部分沿用，不再多一个线程读 Office。FullCleanup、重绑和退出只在 owner 执行，并在释放前停止接收本绑定请求、使 pending 请求失效。
5. callback 仅推进 session/generation、置 refresh-pending、唤醒 owner；尽量不在 callback 内遍历 Office 图、覆盖 root、解绑或等待 owner。owner 根据当前 binding 重新单次取得窗口并按转移规则安装，从而避免把 callback 参数直接当长期持有。
6. mailbox、session/cache 锁只用于纯值、队列、请求状态；出锁后执行 Office 调用、订阅/解绑、Release、等待。COM 会泵送事件，不能在 callback 等 owner，而 owner又等 callback 返回。
7. 同线程内部 owner helper 不能重新走“入队后同步等待”公共接口；需直接调用内部执行函数，并保留会话校验，防自等死锁。原子 lifecycle/cache 更新可在 callback 执行，旧 root 的 Release 由 owner 延后至当前作用域临时对象已释放后处理。

最小受影响范围：三个 C# 文件及现有 managed tests；正常不需改旧 COM 方法顺序/GUID、TLB schema、native 业务 API，除非实施时发现已有异步语义要求无法保持。不要为此改 native 的 COM apartment 或整体线程启动方式。

#### 7.3 请求过期、取消与执行时序

- 请求捕获 expected binding/session/window；执行前与取得 View/SlideNavigation 后再核对，拒绝旧文稿/旧场次操作；命令中不重新获取“最新窗口”来执行旧请求。
- generation 不应被机械用作所有队列命令的“页变化就丢弃”条件：快速连续 Next 应按用户顺序执行；它们通常应匹配绑定和场次，而 End/annotation 的迟到确认保持更严格身份条件。
- 队列有上限、service Stop/重绑会完成或取消未开始请求；按现有限定有界等待，禁止无限排队或轮询等待占住 UI。
- 建议请求显式区分 `Queued / Executing / Completed / Cancelled`。未开始请求在超时时原子取消，owner 此后不得发起 COM 操作；owner 声明执行与取消有单一竞态决胜点，声明后不持锁调 Office。
- **已进入 Office 的 COM 调用不能因托管等待超时而撤销。** 当前 `SlideShowExitRequest.Wait` (`SlideShowSession.cs:228`) 超时标记与执行检查之间仍可能交错；不能写“所有超时绝不晚执行”。执行中超时应按旧 API 能表达的失败/未知处理，禁止自动重试造成双翻页，后续通过纯值状态确认。
- annotation 一次请求内可能有 laser off、pointer reset、再次查询；阶段间需检查绑定/场次和取消，防止 View 获取或可重入调用改变目标后继续操作。

#### 7.4 与在途调用屏障比较

| 方案 | 代价/收益 | 本次建议 |
| --- | --- | --- |
| 复用 owner mailbox + 纯值 getter | 可复用现有线程/唤醒，清理天然排在当前命令之后；需补齐旧公共命令与 callback 边界 | 优先。范围集中在 interop，避免继续共享 root。 |
| 接收门+每 binding 在途计数+清理等待 drain | 表面改动可能少，但每个 public/事件/异常分支都必须进入/退出；callback 触发清理等待当前调用会自锁，且不解决同时调用 Office 本身 | 仅在具体旧同步行为无法经 mailbox 保持时选用；不能只加一把大锁。 |

若不得不用屏障，应先短锁拒绝新借用并标记 closing，释放锁后等计数归零，只有 owner 清理；callback 只能标记 deferred cleanup，不能在自己的在途段内等待归零。不要用 `lock(rootGate)` 包住所有 COM/Unbind/Release 或在锁内 `WaitForPendingFinalizers`。

### 8. 事件参数 WnObj 的所有权决策门

`SlideShowChange`、Begin、End、BeforeClose 的参数首先是 PIA/COM event adapter 传入的托管引用，handler 内增加局部变量不构成 acquisition。End/BeforeClose 还可能是 Presentation 而非 Window，不能对全部 `WnObj` 一律按 window 释放/保存。

本次应避免旧代码“视为 borrowed 却长期缓存”的模糊状态。建议先从 handler 撤下长期保存：callback 仅发纯值生命周期通知，owner 自己调用属性取得并持有 window。与此同时，**要单独查明并验证 event sink 的 RCW receipt**：如果该 native→managed 参数映射为本模块新增 RCW 计数，则由唯一拥有该 receipt 的事件适配边界 finally 单次配平；多播托管 subscriber 只是借用，不能各自 Release。若 adapter 已管理这笔映射，handler 再 Release 就是重复。没有证据时不在所有 handler 粗暴添加 Release，不能用 FinalRelease 抹平。

验证需要 native COM fixture/实际 PIA sink 触发重复事件：同一参数 identity、同 identity root、不同 window、回调抛错、重入 Begin/End，记录入站边界与 root acquisition；既检查 callback 完成后的计数增长，也检查其他 subscriber/长期 root 未失效。此验证无需启动 Office，可作为实施期 headless 测试，但本轮未实现或执行。

### 9. 推荐释放账本及最小修复顺序

每笔记录至少包含 `binding/session + source stage + receipt id + identity(for compare only) + owner slot + transferred/released`。生产常驻日志不必记录 COM 指针或用户完整文件名；测试 seam 必须能给同 identity 的多次返回分配不同 receipt id。

| 来源边界 | 初始 owner | 合法转移 | 退出配平 |
| --- | --- | --- | --- |
| GetRunningObjectTable / EnumRunning / Enum.Next / CreateBindCtx | scan/iteration | 不转移 root | 各自 finally，逐项 best effort。 |
| ROT.GetObject 应用返回 | candidate | scan list -> 返回 best -> binding app | 每一笔只一处负责 Release；落选/重复立即或扫描结束配平。 |
| ROT.GetObject 文稿返回 | iteration | 无 | 文稿用于取得 Application 后仍单独 Release。 |
| 文稿.Application 返回 | candidate | 同 bestApp 流程 | 与 ROT 文稿是不同取得，分别配平。 |
| Application.ActivePresentation 返回 | temporary 或 binding presentation | 仅绑定安装时可转移 | 比较/轮询临时总是 Release；转移后局部置空。 |
| Presentation.SlideShowWindow 返回 | temporary 或 binding window | 确认替换时单笔转移 | 相同 identity但未转移也要 Release。 |
| callback WnObj | 明确的 event adapter receipt/借用 | 建议不直接转移到 root | 先验证 sink receipt，再确定唯一配平边界；不得按变量数释放。 |
| root.View / View.Slide | local command/reader | 不转移缓存 | finally Slide->View；每个获取一次。 |
| Presentation.Slides / Slides.Item | local reader | 不转移缓存 | 每页 Item finally，最后 Slides。 |
| Application.SlideShowWindows | local count helper | 无 | finally。 |
| window.SlideNavigation | local View command | 无 | finally。 |
| root 的赋值/as/cast | borrowed alias | 不新增 receipt | 不额外 Release。 |
| GetIUnknownForObject 指针 | identity helper | 无 | `Marshal.Release(pointer)`；非 ReleaseComObject。 |
| 正常字符串、数字、bool、enum、DTO/JSON | managed value | 纯值缓存 | 无 COM release。意外 COM 返回则拒绝并按该笔 Release。 |

建议实施顺序：

1. 先建立根持有/临时转移/ROT ledger 和失败注入，修复已确定的重复、漏配平以及命令 temporary；不把 GC 或 FinalRelease 当测试通过条件。
2. 改生产常规分派为实际 dynamic，保留 reader 原有子先父后 finally；按能力记录 HWND/事件的受控例外。确认异常分类不把 busy 当接口不支持。
3. 事件订阅位图及部分失败回滚，厘清 WnObj receipt；将所有 live root 操作收束到已有 owner/mailbox，getter纯值化，再审计 FullCleanup。
4. 清理只释放本模块明确持有，逐个处理异常；移除对强制 GC 的功能依赖。退出时 `FinalRelease` 不得作为漏配平补偿，也不能作用于可被其他路径/组件共享的 RCW。若旧 WPS确需专用最终释放例外，须证明独占所有权且调用已静止，并单独记录支持矩阵证据；缺证据不保留为默认。
5. 最后与本任务 busy/session/HWND/native 门禁修复整合，保持文稿/会话隔离，做完整回归与真实设备退出验收。

### 10. 验证方案：不靠 GC、不得结束用户 Office

#### 10.1 Headless 账本与真实 COM fixture

当前 tests 只证明 reader/session exit 的 fake 访问序列 (`PptCOM.Tests/Program.cs:15`、`:178`；`SlideShowSessionTests.cs:230`)；批注测试 `:363` 使用普通 managed `FakeAnnotationView`，`Marshal.IsComObject` 为 false，不能证明其 RCW 生命周期。现测试没有覆盖 ROT 候选替换、bestApp相同 identity、窗口转移、public命令临时对象或部分事件绑定失败。

应增加具有实际行为价值的用例：

- 两个不同 receipt 同 identity：未转移的每个属性/ROT返回各释放一次，root仍可用；单笔被加入列表与设为best不增加释放次数。
- ROT A->B->C优先级递增、重复 moniker、target同best、active文稿暂不可读、每个异常点：唯一获胜返回一笔，所有落选项一笔，去重期间无已分离RCW。
- window沿用、root空、root失效真实替换、同identity无替换、异常/Busy、后续poll覆盖：每次取得都转移或释放，无 orphan receipt。
- Next各 polling分支、Previous、legacy End、SlideNavigation缺失/成功/中途异常：temporary都finally配平，不调用GC/FinalRelease。
- reader逐属性/每页Item异常、SlideID缺失、结束页、状态查询失败、标量意外COM返回：保留现有ledger；补实际dynamic生产分派测试，不能只mock字典。
- 每一个事件add/remove故障点：成功项可枚举、全回滚、无重复订阅；只有connection point失败时动态页码/翻页仍可轮询。
- service重绑/Stop与并发队列、请求未开始超时、执行中超时、Office调用内重入Begin/End：无死锁、旧场次命令不作用到新场次、每个取得仍配平。
- COM fixture提供IDispatch但拒绝PowerPoint类型IID，标记/计数每个Invoke/QI/AddRef/Release，验证实际dynamic路径、连接点失败后轮询以及引用释放。它模拟接口能力缺失，不等于复现用户全部注册损坏类型。

Fake ledger 是业务配平验证，不能代替实际 CLR RCW/COM fixture 或真实 Office 退出；反之不应通过让 `FinalRelease` 把所有计数清零来掩盖 ledger 不平衡。

#### 10.2 后台退出与下一文稿可打开（需另行明确 GUI/Office 测试授权）

本轮用户只批准调查规划，仓库规则也不允许默认启动 GUI，所以下列只是验收计划，**NOT RUN**：

1. 在隔离测试用户/VM中记录运行前 Office进程基线，明确由测试创建的进程PID、创建时间、可执行文件身份和文稿路径；不能把同名新旧进程混为同一个。PowerPoint/WPS可能复用既有进程，无法证明自有就停止自动清理。
2. 每个被支持的 PowerPoint 2010/MSO14、WPP2013实际版本/位数，以及当前维护版本分别跑事件正常、类型接口不可用但IDispatch正常的轮询模式；不要为测试破坏用户注册表/Office安装，故障环境使用快照VM/明确 fixture。
3. 循环100次换页/前后页/原生批注读写，反复结束再开始、A/B文稿切换、busy、关闭文稿、退出Inkeys和退出测试创建的Office；持续观察引用/队列归零，不调用GC辅助。
4. 只对测试创建且仍确认归属的Application发Close/Quit，文稿由测试无修改或明确丢弃。生产联动正常cleanup不应自行Quit用户Office。
5. 用进程句柄等待真实退出，记录每阶段时间（如1/5/10/30秒的在场状态、退出码），区分Inkeys附着期间、停止联动后、Office Quit后；“最终过很久退出”不能算及时释放验收通过。最终阈值由干净无联动基线与维护者决定，本报告不凭空保证固定毫秒数。
6. 退出后立即打开下一份自有测试文稿，确认无需等待旧进程、不出现文稿打开阻塞；同时确认同机用户原有Office进程/文稿继续运行。
7. 失败时收集自身receipt账本、会话、订阅、在途命令和进程状态；不使用 `taskkill`/`Stop-Process`/按进程名kill作为通过标准，也不关闭未知Office窗口。超时残留保留证据，由测试环境所有者处理。

### 11. Related specs 与实施时需更新处

- `.trellis/spec/ppt-interop/com-contract.md:51` 的反射默认违反本次用户dynamic优先；`:52` typed HWND例外要改成逐能力、dynamic证据优先；`:55`“WPS所需的final release/GC保留”不能继续作为允许依赖GC的合同。
- 同文档 ownership、子先父后 finally、纯值缓存和禁止FinalRelease borrowed root 的部分应保留，并补充“每次取得 vs identity”、“明确转移后置空”、“只有owner释放root”、“部分事件订阅回滚”与“callback receipt”约束。
- `.trellis/spec/ppt-interop/native-session-ui3.md` 应准确描述owner现覆盖的公共方法、超时只能取消未开始操作、执行中COM不可撤销的边界。
- `.trellis/spec/ppt-interop/index.md` 的支持声明继续分“代码意图/实测”；实际MSO14/WPP2013覆盖记录必须写明版本/位数/安装类型和NOT VERIFIED项。
- 历史任务设计/研究仅作为证据，不应批量重写归档。当前任务获批后通过主会话的update-spec流程修订现行规范；本研究角色不修改spec。

### 12. External references

以下均为官方资料，于2026-10-18检索；不是Office/WPS实机验证：

- [Microsoft .NET Runtime Callable Wrapper](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/runtime-callable-wrapper)：对象identity与多个托管别名的关系、COM接口缓存；跨apartment代理可能是不同managed对象。
- [Marshal.ReleaseComObject (.NET Framework)](https://learn.microsoft.com/en-us/dotnet/api/system.runtime.interopservices.marshal.releasecomobject?view=netframework-4.8.1)：重复入站映射影响RCW计数，以及共享对象/正在执行调用的释放风险；适用范围包含.NET Framework4.0。此处不采用其循环归零建议来处理共享RCW。
- [IRunningObjectTable::GetObject](https://learn.microsoft.com/en-us/windows/win32/api/objidl/nf-objidl-irunningobjecttable-getobject)：成功返回调用AddRef，由调用方Release。这支持bestApp返回的明确所有权。
- [COM reference-counting rules](https://learn.microsoft.com/en-us/windows/win32/com/rules-for-managing-reference-counts)：原生in参数的嵌套借用与out/返回值所有权；不能直接把原生规则替代.NET事件marshaling检查。
- [Events in COM and Connectable Objects](https://learn.microsoft.com/en-us/windows/win32/com/events-in-com-and-connectable-objects)：事件由客户端sink接收，支持将事件包装边界与普通对象读写分开。

## Caveats / Not Found

- 本轮没有执行git操作，因此本文件不独立给出每个旧问题首次出现的提交；“新增/既有”的最终历史归属需要父会话其他研究合并。当前路径和行号以本次读取源码为准。
- 没有构建、运行已有tests、COM fixture、Office/WPS或GUI；所有退出时间、事件sink RCW receipt、类型接口损坏设备行为均未实测。不能将source-level不配平直接等同于用户进程残留已复现。
- 不存在可由本轮静态代码保证的“任意COM损坏仍可用”；dynamic依赖可用的IDispatch及对象访问，注册损坏、连接点失效、缺少类型库、跨位数/安装问题需分型。
- 旧研究与现行spec的“事件参数borrowed”、反射默认、FinalRelease/GC描述相互存在语义缺口；应在当前任务设计中明确边界，而非实施者自行猜测。
- 本文件为独占研究产物，只写此文件及其research目录，未覆盖其他研究员文件。
