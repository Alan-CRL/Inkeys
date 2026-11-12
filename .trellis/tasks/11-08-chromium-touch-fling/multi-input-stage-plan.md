# 多设备漫游与惯性设置：最终规划摘要

状态：用户已批准本摘要并要求开始实施，进入实现与非 GUI 验证。任务仍为 chromium-touch-fling / in_progress；不自动 commit/push/结束任务。保留用户确认的 961ffdf8 体验基线。

## 目标与范围

白板拖动/平移模式支持鼠标、触摸、笔尖、笔尾及触控板双指滑动，按输入类型控制释放后的惯性。用户已明确触控板仅双指滑动，按压拖动暂不新增支持；已有系统转鼠标行为保持。

## 已确定与本方案提出的决定

- 拖动模式所有来源的惯性不因笔尖/笔尾靠近急停；真实落笔 Down 取消旧惯性，依活动来源仲裁开始新导航。绘制模式不在本阶段扩展。
- 设置放在“绘制 → 通用”，预测设置之后/橡皮组之前；一条“白板拖动惯性”父开关，下方五项内联选择。复用现有宽窄布局与条件动画，父关闭收起、保留子项。
- 总开关方案默认开启。五项默认如下；关闭任何惯性仍允许对应输入跟手平移，运行中关闭则停止自由运动，重新开启不复活旧速度。

| 输入 | 惯性默认 |
| --- | --- |
| 鼠标 | 关 |
| 触控板双指 | 开 |
| 触摸 | 开 |
| 笔 | 开 |
| 笔（笔尾） | 关 |

- 触控板采用系统双指手势与原生惯性手感；另外四类沿现有白板释放/摩擦曲线。两者只允许一个活动来源，禁止叠加惯性或跨来源继承残余。
- 触控板接线目标为 Windows 10/11 精确式触控板（PTP）。旧系统或仅提供普通鼠标/滚轮的驱动保留原功能；不靠 wheel 频率/数值猜设备、抬指或尾段。该限制不缩减原有产品能力。

## 技术边界与证据

完整需求/AM 验收在 prd.md；设计在 design.md；实施清单在 implement.md。输入与系统 API 证据在 research/multi-input-roaming.md，设置/配置证据在 research/multi-input-settings.md。

采用现有真实 ContactInput 历史接笔/笔尾，保持 Down 水位/Stop/overflow 及 961ffdf8 节拍。触控板使用稳定 Direct Manipulation 的手动更新/累计变换方案，只输出数值给现有 Draw3 viewport，不接管呈现树。COM 与回调留 HWND owner，绘制线程只消费带 epoch 的数值；有界保序边沿、累计变换合并和普通更新不发控制 wake，避免再次造成消息阻塞感。

配置使用 Drawing.Whiteboard.PanInertia 六布尔字段，main.json/NoUpload/缺项默认；启动与设置经 Host 完整快照发布。普通 Move 不读写配置或重复唤醒；绘制线程统一应用热切换。

## 验收与风险

- 真配置/真设置控件覆盖五项默认、持久化、主子偏好、显隐禁交互及二十语言。
- 真实 Pen/尾准入、持续接触不自我 Stop、所有来源 hover 不停/Down 停旧、Up 不丢末点、跨源/Stop/overflow 不复活。
- 触控板双轴跟手、系统 INERTIA 开关、重复消息不双算、READY 重置无回跳、页/模式/尺寸/失败隔离；保留 Mouse 与新增 Pen hover/触控板更新的 deadline 回归。
- 原生 ARM64 InkeysRepo.sln Debug|ARM64、适用专项和完整 Headless --no-window、i18n check、编码/EOL/diff 检查。
- 实施时仍需验证真实 PTP 消息、RTS 共存、异步 owner pump 的分段延迟和系统惯性取消。无窗口模拟不等于真机支持；GUI/Computer Use 未授权，不自动启动窗口。

## 范围外

屏幕尺度/profile、缩放/旋转、绘制模式漫游/靠近急停扩展、触控板按压拖动独立识别、鼠标右键漫游、RTS 后端/硬件时间戳、摩擦调参页及呈现树重构。本阶段不自动 commit/push/finish-work/archive。

## 规划门检查

PRD 已按当前目标、证据、M1–M9 需求、AM1–AM8 验收及范围外重新收敛；旧阶段原文/锚点保留在历史分节。design/implement 与当前阶段清单已齐备；implement.jsonl/check.jsonl 仅保留本阶段所需真实 spec/research 引用。用户已确认本摘要（含系统触控板惯性及兼容范围）并要求实施；当前阶段实现与自动验证已完成，真实PTP/RTS/光学另列未验，不以模拟数据宣称真机通过。

## 实施状态

已完成本阶段代码与独立静态审查；原生ARM64最终完整solution及配置/control-fence/pan-feel/Headless pan/setting-general/diagnostic-output专项通过，完整Headless --no-window已exit0（575.61秒）。实际验证、历史修复与产物哈希见 multi-input-validation.md；源码保持冻结，不作GUI，不自动提交或结束任务。
