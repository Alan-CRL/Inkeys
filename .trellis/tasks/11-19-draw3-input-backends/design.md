# 第4步设计

## 输入选择与生命周期

在当前 Host 初始化边界分开保存用户偏好、大屏模式约束、选定生产者与实际运行结果。后端只在 Host 启动时解析一次；运行期修改输入方式或大屏模式只改变“下次启动将使用”的显示，不热切换生产者。大屏优先级最高；其他模式按 PRD 矩阵解析。Pointer（自动或手选）初始化失败时尝试 RTS；RTS 沿用扩展、基础 XY、鼠标回退顺序。启动内回退时先终止已启动的生产者、触点和导航入口，再接纳新代次。动态解析较新系统 API，保持 Win7 加载能力。复用 UI 的设备判别和已有画布光标／触觉逻辑，确保普通鼠标可写、提升消息不会重复发布。

## 两个生产者，一份快照

- RTS 在 RealtimeStylus 解码端处理单位、Y 符号、负仰角。区分直接角和双轴来源，验证真实笔向；不将裸 HID、RTS 与 Pointer 的方向约定混用。
- Pointer 在画布消息线程读取 PT_PEN／PT_TOUCH 当前消息的原始信息，映射屏幕／客户区坐标。有效笔压除以 1024；有效双轴角按 Pointer 坐标约定转 Modeler 弧度。触摸原始框换算画布宽高并标记来源单位。宽高仅供触摸判定。缓存 sourceDevice 与本次触点的绑定，处理捕获丢失、取消、设备移除和抬笔。
- 实际包缺失字段记未知，合法 0 保留。当前笔的最近有效值与原始 mask 分列；首包真正传 −1 后，当前 Modeler 相应投影字段持续未知直至 Reset。首轮保持最新 Move 策略及既有时间基准，另录 Pointer PerformanceCount 和到达 QPC 作比较。
- 普通笔进入协调器和固定帧消费者，触控板帧单独进入白板导航。保留源类别，设置和几何层无需按 RTS／Pointer 分叉。

## 导航与笔锋

Pointer + DM 复用现有 viewport、触控板命中筛选和累计位移桥。DM 与 Win11 触控板消息共用白板拖动的位移入口，均要求当前为白板且已选中“拖动”工具。先初始化绘制生产者并确认实际后端，再按系统和后端选择导航入口。Win8.1/10/11 的 WM_POINTER 路径使用 DM。Win8.1/10 的 RTS 路径正常启动时跳过触控板导航初始化；若作诊断探测，只能在 RTS 初始化成功后尝试，预期失败时记录错误并清理，不影响 RTS 书写。Win11 + RTS 在 RTS 就绪后注册窗口级触控板消息，按帧送 Interaction Context。SDK 10.0.26100 无 `RegisterTouchpadCapableWindow`、`GetPointerFrameTouchpadInfoHistory` 等声明，按微软文档手写结构与原型并 `GetProcAddress` 解析；Interaction Context 入口以系统实际导出为准（本机 ninput 有 `RegisterOutputCallbackInteractionContext2`、`ProcessPointerFramesInteractionContext`，无 `…2` 版处理函数），任一缺失即报告手势不可用。两条导航路径均以 inertiaOptions_.Allows(CanvasPanInputKind::Touchpad) 决定是否消费惯性位移，原生惯性只应用一次。关闭惯性时终止续滑；切回绘制时取消手势、废弃旧代位移并释放旧入口。Win8.1 命中消息版本记录与实现先例有差异，需实测全链路才标为可用。

普通笔根据落笔设备和设置取得笔锋开关。用局部压感半径乘 `1 − 0.5 × (1 − M)`，M 为现有逐点笔锋倍率。保持 55 ms Normal 参考窗口、端点位置追赶和尾长收敛。对派生尾段施加一次倍率，沿用 L1 保护窗口和公切线约束；正常提交与异常保存采用同一笔的策略。
