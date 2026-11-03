# R2 诊断版采集说明

## 开启与保存

沿用 设置 → 实验选项 → 控制台输出 → 测试触控反馈。已开启的用户仍需退出旧进程、启动本轮重新构建的新版；运行中只改开关不启用本轮采集。无需改变Windows Ink或驱动模式。

保留从启动到复现后的完整输出（stdout和stderr），不要只复制最后几行或仅普通log/idt文件。现有初始化会保留标准流的文件/管道重定向；也可以按之前方式复制控制台。新版应出现 `[PenHaptics] event=ui_schema v=1` 与 `event=ui_input v=1`。保留 Draw3Diag build行及 DiagnosticOutput掉样/截断统计。

## 一次采集建议

全程只用M6，先点一个主栏按钮，再点左右翻页控件，往返几轮；出现无响应时继续点同一目标两三次，保留失败和恢复前后，不立即重启或改设置。最后可将笔移出感应范围再返回，观察是否恢复。结束后说明大致操作顺序及哪些步骤无响应，不必逐条解读日志。

## 关键字段与阶段

- role/hwnd/tid：区分主栏和四个分页窗口及生产/消费线程。
- msg/extra、source/phase、time/tick、point及其来源：区分原生消息、系统签名、批次头和实际解码触点。Pointer原始类型未知时明确unknown，不猜成Mouse；raw批次phase=batch不等于真实接触Down。
- contact/generation/device、batch与channel：将一次触点沿窗口/队列/会话关联。消费日志若标generation_basis=surface_lifecycle，表示窗口注册生命周期，不是指针会话代次；source_basis说明来源依据。数字Pointer ID不等于物理设备ID，也不能自动等同TOUCHINPUT.dwID。
- stage/reason：raw/filter/touch_read/touch_select/lease/capture/queue/consume/session/action等首个停点。
- requester/owner和双方generation：确定非鼠标租约是否由另一窗口占用；与客户端channel→role/hwnd映射一起阅读。owner_snapshot=before_mutation表示操作前持有者，成功acquired后的新持有者是requester。
- rect、presented hit/widget与版本、visible/locked/mode：区分窗口收到但未命中可交互主体、工作区尚未就绪和旧语义请求。no_main_button只指主按钮表，不自动代表属性面板没有命中。
- suppressed/truncated与输出掉样统计：Move经过限频，缺Move记录是预期；按下/抬起/取消/拒绝按边沿记录。输出队列本身仍有界，不能把掉样当OS没投递。
- action的业务接受/回调派发，不等于画布呈现或PPT已执行ACK。原bar_click accepted/attach sent只属于可选触觉，不作为UI成功失败判据。

## 这次需要分清的两条链

1. Pointer可见→原始Touch是否可见→读取/触点选择→lease/Session拒绝或入队→命中与业务。
2. 右下Touch进入Bar→当时窗口rect和已呈现命中→是否取得capture/lease→分页收到何种消息及为何接纳/拒绝。

本轮只补观测，没有修复点击或改变输入策略；构建/无窗口测试结果见 diagnostics-validation.md。下一次真机日志才用于确认现场根因。
