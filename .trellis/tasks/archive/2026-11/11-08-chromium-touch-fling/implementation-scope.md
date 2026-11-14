# 当前实施范围（用户已批准，2026-11-08）

本文件优先于原阶段1 Chromium逐字复刻要求及repair-plan中的待评审表述。用户已同意白板手感优先，并明确本轮实现当前电脑的鼠标/触摸惯性、连续滑动和设置诊断；屏幕尺度/EDID/设备profile推迟。禁止GUI、commit、push。

## Requirements / Acceptance

- R1 两类输入（鼠标左键、Touch）移动模式都正常跟手；接触立即停旧fling，笔hover/落笔停止及模式/页面/生命周期栅栏保持。
- R2 真实Move不得被Up覆盖丢失；Down/Move/Up/Cancel有统一导航消费顺序，不丢跨帧Down；同一输入不同呈现fps不得改写真实轨迹。记录缺口，overflow安全终止当前导航epoch并吞旧触点至终态，不由Move复活。
- R3 释放仅使用可靠新尾段，明确反向先废弃旧速度/boost，证据不足释放为0。初始采用最近同向线性拟合窗口50ms、至少3点20ms；允许在数值/测试证据下做必要边界细化并记录。30–70ms输入年龄软衰减；不要恢复250DIP/s距离台阶。
- R4 自由惯性以708向量摩擦模型为基准（tau=.30s,c=1000DIP/s²），不恢复Slipping。二维方向保持。Down停止旧动画，后续几何不混旧惯性。连甩按当前残余在新方向的正投影而非历史初速叠加，模长上限12000DIP/s；原50ms滚动续期语义；反向/笔/取消/弱证据/跨设备清候选。
- R5 区分输入释放时间和动画呈现起点，首消费释放帧不追补历史惯性，之后使用解析时间积分；不宣称消除渲染器自身长帧。
- R6 设置→软件调试→控制台输出增加独立“白板平移与惯性诊断”运行期控制能力，按现有配置持久化/启动控制台约定（可标重启生效）。不依赖DRAW3_RTS_DIAGNOSTICS宏，不全局打开旧宏。默认关闭。使用已有异步有界DiagnosticOutput，记录事件时间/消费延迟、关键状态/停止原因、raw/release/boost速度、样本数/丢弃原因/队列溢出；关闭无高频格式化，输出失败可见，不无限打印Idle空帧。
- R7 增加真正穿过生产协调器的末反向Move+Up、跨帧Down、短反拖、hitch后立即Up、变指后立即Up和30/60/120/240fps重放测试。现有黄金值保留为参考组件测试，但不强迫产品沿用其释放/曲线/booster行为。
- R8 原白板视口、Freeze/背景、多指基线、设备互斥、绘制/橡皮输入、低功耗等待不退化；编码/BOM/CRLF和minimal diff。

## Design / Boundary

输入修复优先复用ContactInput生命周期增加导航专用有界事件历史，原绘制latest mailbox不改语义。producer Down开始保留而非consumer Register后订阅；连续发布水位与消费截止必须真实成立。复用handle/generation并独立导航epoch。导航所有权和RTS/鼠标回调是数据事实，不用异步读每指latest代替有序批。

本轮至少保留所有回调级真时间样本，不把RTS批内点均分伪造时间。硬件timer支持/批内全样本作为后续工作，记录当前timestamp来源/采样损失边界；不切换Pointer后端、不动设备尺度。可检测短间隔/异常时间用保守估速，不能承诺从arrival还原不可观测真时间。

异步多指：位置focus可用各指latest-known，估速只能用该指真实测量时间，不凭另一指事件添加伪静止样本。pointer-up相反方向清窗与几何基线保持；新Up只闭合而不虚构Move速度。

保持外部CanvasPanFrameInput/Output/Step/Stop/ClearAxis等接口尽量稳定；必要增加诊断字段。保留参考Chromium组件供现有数值测试；产品门面切到批准的可靠释放/向量曲线/残余投影策略。避免并存两套产品控制器或大范围重构。

## Ownership / Execution

1. 输入代理负责ContactInput.cpp/.cppm、RealtimeStylus必要最小采样诊断、DrawingController.cpp（含生产探针）与输入专项测试；给导航门面喂有序真实样本，接日志新增字段时与诊断/内核代理沟通。
2. 内核代理负责CanvasNavigation.cpp/.cppm和draw3_canvas_pan_tests.cpp；可靠释放、连续Move不丢历史、多指真实估速、曲线与boost、CPU测试。
3. 诊断代理负责Diagnostics.cpp/.cppm、配置/IdtMain、Setting、i18n及必要生成资源；提供IsCanvasPanDiagnosticsEnabled/配置接线等清晰API，不编辑DrawingController。
4. 主会话整合、更新spec/task、实际ARM64编译/无窗口回归；代理不并发MSBuild。不独占工作区、不还原他人改动。

每段有据失败先回归，不为通过而放宽断言。复用现有InkeysRepo.sln Debug|ARM64+原生ARM64 MSBuild（动态定位），同次PowerShell规范Path/PATH并禁node reuse，完整构建至少5分钟。只运行无GUI入口；不新建构建系统或升级依赖。

## Deferred

屏幕物理尺度、桌面/大屏profile、屏幕笔/数位板平移、新设置调参页、真实硬件packet timer、渲染/tile重构。用户实机手感验收不能由自动测试替代。
