# 技术设计：生产笔触觉恢复

## 证据与根因
详见 research/haptic-history.md 与 research/bar-haptic-path.md。连续反馈丢失的确定根因是 fc9a8b86 迁移 Host 时没有创建/注入 PenHapticFeedback；后端与 Controller 书写分支已经存在。离散反馈在可查历史中没有调用者，需要接入当前 UI3 的有效按钮事件。

## 边界与资源所有权
- 生产 Draw3 Host 在唯一绘制线程创建、Initialize、调用和 Shutdown PenHapticFeedback，并把指针传给 DrawingController；同线程 RAII 覆盖启动失败、Run 异常与正常停止。RTS、Renderer、两套 HWND 的现有所有权不变。
- Win32/Bar 窗口线程仅观察当前 Pen 元数据并发布有界请求；UI3 交互线程只确认业务接受，不跨线程调用 WinRT/COM。
- 不从 Bar 调用 ForwardProductMessage：那是画布光标/输入入口，会改变 cursor owner。新增最窄的触觉发布 API，经产品现有停止门和 Host 控制唤醒路径到唯一 owner。
- 触觉请求与画布 Clear/Undo/保存命令分离；过期、满队列、Host 未运行/停止均只拒绝可选触觉，不改变按钮原业务动作或挤掉保存屏障。

## 连续反馈数据流
`Drawpad WM_POINTER Pen -> 原WindowController触觉mailbox -> DrawingController -> PenHapticFeedback`。
RTS Down 锁存的真实工具/笔尾决定实际书写波形。绘制可用性只决定连续书写反馈；当前有效工具点击的短Click走独立UI请求，不因该点击本身改变Selection状态而误当作程序刷新。保持 Pen/HardPen/Shape=Ink、Highlighter=ChiselMarker、Eraser=Eraser、inverted=Ink；Laser无连续反馈。

已有 Touch Pan 路径清 pending、发布 leave 并停止反馈，必须保留。启用原模块后，必须局部收敛原来隐藏的预启动生命周期风险：Hover 成功预设要有清晰状态追踪；selection、隐藏/不准入、Laser、离开/取消、场景隔离时显式 Stop 并丢弃旧绑定请求，不能等待仅由 active runtime 设置的 bool。Host finally 同线程 Shutdown 兜底异常重抛跳过 Controller 末尾 Stop 的路径。

## 兼容和设备路由
- 复用原系统目录绝对路径加载 combase.dll、GetProcAddress 和 WinRT activation factory 探测；不静态引入 Win7 不存在的 API，不升级工具链。
- PenDevice 的明确 pointerId 与 SimpleHapticsController 是本次生产精确笔路由。新设备绑定失败必须清旧 controller，不能继续给上一支笔发反馈。补充解除绑定/重置失败缓存的窄入口以处理离开、取消与 pointerId 重用。
- 后端存在 InputHapticsManager.GetForCurrentThread 回退，但该 API 依赖输入线程；不得从绘制线程创建一个 manager 并宣称是原笔。生产明确绑定路径禁用这种泛设备后台回退；不具备当前笔控制器时安全无反馈。保持原 demo 默认行为与生产选项区分，避免无关修改历史 demo。
- 若实现保留新 manager 辅助路径，必须由实际窗口输入线程取得既有 manager，验证相同当前笔身份，并确保不改变上述无能力 no-op 验收；不能仅凭 deviceType=Pen 路由。该扩展不是 MVP 所必需，默认不实施。
- SupportedFeedback 中优先目标波形，缺失时回退到必需 InkContinuous/Click；基础波形也不可用或 HRESULT 失败时返回 false。默认强度仍取系统设置。

官方依据：
- https://learn.microsoft.com/en-us/windows/uwp/ui-input/pen-haptics （能力探测、必需波形、Hover预设与离开显式Stop）。
- https://learn.microsoft.com/en-us/windows/apps/develop/input/haptics （GetForCurrentThread须为UI/输入线程；后台线程无法正确路由）。
- https://learn.microsoft.com/en-us/uwp/api/windows.devices.haptics.inputhapticsmanager?view=winrt-28000 （TryGetForThread只取得已存在manager）。

## UI3 离散反馈合同
详细来源和 token 方案见 research/bar-haptic-path.md。离散触觉限于画笔/橡皮工具与四个笔类型按钮，只附着在真实 Pen 的现有 WM_TOUCH 翻译/有效按钮动作上；不消费 WM_POINTER、不合成第二份点击，不删除原兼容鼠标去重，不改 RegisterTouchWindow/capture/系统光标。

WndProc 在原消息有效期内动态查询 Pointer API；TOUCHEVENTF_PEN 可以确认 Pen 来源，但 TOUCHINPUT.dwID 不可直接当 pointerId。Pointer 观察与当前触摸接触必须能唯一关联，否则保留按钮动作并放弃可选触觉。Microsoft文档未保证Pointer与WM_TOUCH投递关系或两套source handle恒等；即使严格核对同阶段、屏幕点、系统时间、source handle，也须保留驱动关联失败时的no-op，真实设备可达性待验收，不能把相近位置当身份。通过私有、有界的 per-contact token 保持来源到交互线程的已接受操作边界；不能用全局最近笔或坐标单独推定身份。

保留 ExMessage 的既有屏幕坐标、取消、触摸标记语义和 PrepareBarInteractionMessage 归一化。接触取消/离开/设备变化、Host代际变化、token回收/超时均使请求失效；未消费token不得被无界重用，避免旧队列消息命中新接触。满槽只省略触觉，不阻塞/丢失原交互。

## 范围、取舍与验收限制
本任务恢复生产接线和必要的安全生命周期，不增加全UI/触摸板/鼠标触觉，不新增用户配置或工作线程。新 manager-only 设备扩展留后续独立任务；实体设备体感及真实 Win7 仍需设备验收。本轮自动验证不启动GUI，也不能用纯逻辑或编译冒充笔体感实测。

## 回滚
触觉是可选旁路。逐项撤销 Host 注入、Bar请求/私有token及生命周期补丁即可回到原行为；不改UInk、配置持久化、墨迹格式或窗口构成，无数据迁移。

## 最终实现收敛
CanvasPointerMailbox 统一 Pointer pending、停止信号和 RTS 重置边界。RTS 新 Pen Down 先清旧 pending/绑定，随后新鲜的 in-contact Pointer 才可恢复；RTS 不直接对旧控制器 Tick。活动笔工具与候选波形不一致、多笔或隔离时省略反馈。取到有效新请求时同锁结算此前 leave，避免下一帧误停新反馈。token、按下 session 和 Host 请求共用 shared_ptr 凭据及 atomic invalidation，无需额外设备注册表或线程。组合时序回归在 draw3_haptic_backend.inl，独立 token/mailbox 回归在 draw3_haptic_tests.cpp。
