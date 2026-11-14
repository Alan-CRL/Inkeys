# 独立检查：2026-11-14 屏幕笔释放与移动模式光标

## 范围与结论

检查对象为 c2f21bd0 之后本轮八个产品/测试文件。已读取当前任务 check.jsonl、PRD、design、implement、feedback-2026-11-14.md、相关 C++/canvas-pan/cursor/build 规范及注入上下文。最新用户请求覆盖上一轮只读限制；本轮不提交、不推送、不归档、不启动 GUI。

结论：PASS。静态检查未发现行为错误；构建日志发现一项本轮测试变量遮蔽warning，已在完整测试完成并获得主会话解冻通知后机械修复。没有修改产品源码，也没有与构建/测试并发写源。不能把自动验证替代真人设备验收。

## 源码与边界核对

- CanvasNavigation 只对 ScreenPenPhysical / ScreenPenFallback 开启到达簇处理。短相邻段超限时检查最近且相隔至少2ms的真实锚点；跨段仍超cap则保持原清窗/booster清除。没有可用锚点时只保留未合格证据，不能凭raw点数量起滑。
- 新的拟合证据从最新真实端点反向选≥2ms真实点，再按时间升序使用同一线性拟合；至少三点、20ms。所有raw点仍留在有界历史中参与逐点反向、跟手、末段速度上限和Up年龄。没有伪造或插值QPC，没有把Up当Move，没有扩大速度cap或摩擦尾距。
- 反向确认会截断旧历史后再选证据；异常末端跳跃即使有旧可靠方向也会清窗。容量64与最近50ms左锚点仍按原机制保留；极高到达率下若64点不能提供20ms独立时间跨度会保守拒绝，不扩大容量或伪造时间。
- IndirectDip与Touch保留原护栏、拟合采样顺序和求和顺序；不是用单机屏幕笔反馈重调所有设备。
- 所有当前生产系统光标策略调用（Window ApplyWindowCursor与对应TraceCursorState诊断）都传CanvasPanMode；新增参数默认false，旧绘制调用保持。移动模式Pen/Mouse/注入数位板使用默认系统光标；活动Touch和Touch终态抑制仍优先。WindowFromPoint的其它窗口保护、SetCursor所在owner线程与无ShowCursor计数机制未改。
- CursorOwner()返回的是经过touchCursorSuppressed解析的有效authority，因此最后TouchUp以后不会借持久旧Pen/Mouse恢复箭头。新增真实未附着Window probe验证Begin/End Touch→suppression→新Pen接管→退出移动的完整链。
- 本轮没有改配置、EDID、来源识别、RTS生产时间、后端、橡皮、触控板隔离或工程文件。新增规范与上述源码一致。

## 回归与独立静态证据

- 使用独立Python逐条比较五份C++ fixture与研究JSON：key76=60、77=58、78=77、79=78、80=65，总338条，QPC/px/phase全部精确相等；没有给截断key75补Down。频率10MHz为日志可推得的回放假设，研究JSON已标注，不冒称硬件采样时钟。
- 真实trace分别按逐事件与整批消费进入CanvasPanDynamics，检查释放Ready、非零、held闭合、cap/原屏幕笔摩擦参数，并继续推进实际coast；独立数字拟合误差容限0.1mm/s。
- 物理/回退profile新增安全用例保留全段压缩、原三点压缩模式、跨段极大突跳、同簇末反向、stale Up和末端减速上限。旧Indirect/Touch、连续手势、frame与单位回归保留。
- 共享光标策略矩阵涵盖Pen/Mouse/Unknown、空/有效样本、注入tablet、残留工具、活动Touch、真实Mouse接管和普通绘制。真实Window probe由产品 --draw3-pen-width-test覆盖；新纯光标矩阵由完整headless的RunDraw3ContactInputTests覆盖，不能用仅canvas-pan-only替代。
- git diff --ignore-submodules=all --check：PASS。八个产品/测试文件逐个UTF-8解码成功，BOM与HEAD相同、全部CRLF；尤其DrawingController继续无BOM。无大面积格式化。

## 编译与运行证据

已独立读取 Build/Logs/pan-pen-feedback-2026-11-14/ 的result JSON及关键日志，并逐项复核exit0、sourceBefore/sourceAfter完全一致和日志SHA256：build-3-default-host 32.796s，headless-pan 1.485s，control-fence 1.563s，pan-feel 2.828s，pen-width .204s，diagnostic-output 1.891s，完整headless-all 382.343s。七次验证的八个源文件hash完全一致，完整套件末行PASS animation correctness。没有独立lint/typecheck配置，本项目C++编译即静态类型验证。

此前相同源码构建1的首个错误为C1902 PDB manager mismatch；构建2编译通过后link.exe发生LNK1000 IncrBuildImage内部错误。主会话在宿主环境重试并仅备份本次失败生成ILK后，原ARM64完整solution构建3通过；未通过修改源码、配置、SDK规避环境问题。

构建3有75项warning，其中本轮draw3_contact_tests.cpp:1464循环变量injected遮蔽前面的同名样本，属新增C4456，不能全称旧warning。完整套件结束后仅将循环声明/使用这两个identifier改为injectedTablet，保留原BOM/CRLF和所有表达式。精确差分为Build/Logs/pan-pen-feedback-2026-11-14/test-shadow-only.diff，前后完整文件及hash也保存；其余七个文件字节不变。最终build-4-final增量完整solution通过（exit0，6.985s）；日志只剩一项原有ContactRecord C4324对齐warning，新增C4456已消失。最终headless-pan-final通过（exit0，1.547s）。两组的源码/日志hash已独立核对，均对应当前文件；六个产品文件与canvas回归文件仍与完整套件逐字节一致。完整套件行为证据可按仅两个测试局部变量identifier变化复用，未无依据重复完整测试。

## 未修改事项与能力边界

- 唯一机械自修为上述测试局部变量遮蔽；无待产品决策的本轮阻塞问题。
- 先前late Pointer contact可能取消coast仍只是其它条件性路径；本附件六次屏幕笔释放在起滑前已失败，不据此扩写跨API时钟/身份修复。
- 没有真实设备光标可见性、物理尾滑舒适性、大屏或Win7桌面验收。静态/CPU回放通过后仍应由用户在Surface等目标设备反馈；不能声称所有设备体验最优。
