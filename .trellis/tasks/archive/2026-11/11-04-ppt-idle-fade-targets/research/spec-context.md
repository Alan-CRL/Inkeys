# 实施与检查所需的专题规范索引

以下是本任务所需合同的摘录与定位，便于避免大文件注入截断；不取代原规范。开始实际修改各层前仍按 trellis-before-dev 阅读对应专题段落。

## 现有透明度与配置边界

来源：`.trellis/spec/native-desktop/configuration-i18n-and-assets.md:49` 的“UI3 常驻透明度配置合同”，以及该文档配置审查、国际化生成链章节。

- 背景正常配置为绝对值，0 为主题默认 Dark 80% / Light 88%；背景与内容有效百分比 30~100。
- 主栏/主按钮/分页消费常驻通道；弹出面板、属性栏、菜单保持原透明度。原主按钮和指示的比例不是本任务要重写的材质。
- 配置先按既有系统归属扩展；本任务沿 PPTHelper.IdleFadeSeconds 归属 main.json，不迁移到 deploy.json 或 PptCOM 设置。
- 配置通知只发布目标与请求，渲染线程在帧首应用；Setting 真实事件才入 FIFO 写盘。
- i18n 以 zh-CN 为结构基准，补齐现有全部语言，运行 Scripts/i18n.ps1 sync/check，生成头不得手改。

## 主栏/分页、线程与动画

来源：`.trellis/spec/native-desktop/rendering-and-ui.md:139`（idle 时钟）、`:197`（共享串行调度器）、`:301`（PageControl 与 Bar 按钮单一来源）、`:607`（脏区事务）。

- Bar 和四个分页共用串行渲染线程、独立 target/context，不能让输入或 COM 线程触碰 D2D；渲染回调不能阻塞等待或写盘。
- 共享按钮运行时为唯一标准按钮的布局/输入/动画/绘制/命中来源，本任务只适配闲置策略，不复制按钮行为。
- Idle 前 SuspendForIdle，下次实际回调 dt=0，不能把休眠时长直接推进 0.5s 动画；已有值的 dt=0 保持活动，关闭动画仍直接到位。
- 动画变化需贡献正确 dirty 和续帧；失败帧不能提前提交命中/丢掉必要重绘，资源重建不能使设备外语义状态错误。
- 只通过 Request/Continue/deadline 唤醒，不新增本地渲染循环或计时线程。

## 输入

来源：`.trellis/spec/native-desktop/ui-pointer-session.md` 第 3 节“原有入口与可选补充”“取消与资源归属”及验证章节。

- 主栏输入由共享 worker 消费来源准入后的 Mouse/Touch/Pen 按值消息；不因收到裸 Down 就执行视觉/业务，也不以全局 Raw 注册证明点击有效。
- 原生 Up 不得丢弃，Down/Up 各执行一次；旧取消/旧 captureTicket 不能终止后续新接触。
- Cancel 只清理，不执行点击；UI 闲置恢复不能新增第一次只唤醒而吞业务的分支。
- 拖动、额外透明窗边缘、已呈现几何和属性命中原合同继续生效；不改 Hook/Raw/Touch 的传输模型。

## Setting 控件与验证范围

来源：`.trellis/spec/native-desktop/rendering-and-ui.md:2158` 起的设置布局/一次事件约束，及 `.trellis/spec/native-desktop/setting-motion.md`。

- 复用已有 CheckboxGroup，真实 item 登记完整高度、尾间距及原生滚动范围；窄窗/分数倍率不能只靠手动 cursor 伪造占位。
- UI 副本与一次事件输出维持既有边界，业务通知及写盘留在 Setting.cpp 的 FIFO 域；布局、重绘和 disabled 状态不能触发业务。
- 动画反向接续 current，退出中的不可交互内容不重新获得业务权限。
- AGENTS 要求无 GUI 验证；hidden-window 不等于无窗口。使用已确认初始化前离屏入口、Headless --no-window、Debug|ARM64 Solution，记录真实退出码与未测项目，不扩大平台支持结论。
