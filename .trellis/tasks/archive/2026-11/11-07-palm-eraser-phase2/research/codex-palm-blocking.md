核对分支为 `bugfix/hand-erase`、HEAD=`babdecf6`。已读相关规范及第一阶段归档修订；未修改、构建或启动 GUI。命令进程被环境拒绝，`get_context.py --mode packages` 未能运行，改为直接读取规范索引。

下文 **C/P/I** 分别指 `Inkeys/Inkeys/Drawing/Draw3/` 下的 `Draw3.DrawingController.cpp`、`Draw3.PalmEraser.cpp`、`Draw3.ContactInput.cpp`。

**① 代码事实**

- **范围确为半径外扩**：`D=√(W×H)`、`r=D/2`、`R=r+max(0.15r,4)`，单位均为画布 **px**，4不是DIP。按触点中心距离 `≤R` 判断，不叠加被检查触点半径。观察窗内有效最大面积增长会扩大R；截止后或首次物理Up锁尺，续接不重开窗口。[P:80、108](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.PalmEraser.cpp:80)
- 生产公式唯一调用在 `PalmNeighborBlocked`；它以活动手掌最新已消费位置为圆心，等待续接时用冻结Up位置，并预约到续接期限。两个业务入口是新独立Down和未激活候选观察；只锁住“转手掌”资格，**并未拒绝绘制**，已激活手掌靠近也不取消。[C:2410](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2410)、[12625](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12625)、[12028](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12028)
- 开关为 `Drawing.PalmEraser.ParallelWriting=true`；额外独立Touch还受 `Drawing.MultiTouchEnabled=true` 和 Laser子项默认false约束。[Config:441、445、485](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Other/Other.Config.cppm:441) 设置经 `SetPalmEraserPreference` 保存并发布Host，由绘制owner应用Controller，偏好在独立Down锁存。[State:1305](D:/Project/Inkeys/Repo/Inkeys/Inkeys/IdtState.cpp:1305)、[Host:1740](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.Host.cpp:1740)
- 普通识别候选**先建模，可在识别窗内出墨**；达标后 `PromotePalmRuntime` 准备成功才替换自身几何、清自身Shape/Laser，不清其他笔迹。只有并行关闭且已有阻塞手掌时，新Touch才走 `palmObservationOnly`，不建普通几何，超时/终态回收。[C:2424](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:2424)、[12735、12893](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12735)
- 可复用现有并行关闭收尾：取最后模型快照（续接候选取冻结Up），改phase为Up、清预测，调用 `completeModelUp(runtime,terminal,false)`；另完成Laser、鼠标尺寸会话、手势及笔触觉收尾。[C:17352](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:17352) 它置ended并清续接状态，正常提交后才 `DiscardUntilTerminal`；后续Move不发布，真实Up/Cancel负责回收路由。[C:13065](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:13065)、[18134](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:18134)、[I:775、870](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.ContactInput.cpp:775)
- 真实Up可能先变续接候选；新Down先尝试身份、时间、位置/方向匹配，模型成功后交接handle，随后才做独立准入。普通笔遇大面积续接仍是普通笔。[C:12340、12482](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12340)
- 识别限Touch；现有并行关闭的中断/拒收却也覆盖笔、鼠标。[C:12030](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12030)、[12629](D:/Project/Inkeys/Repo/Inkeys/Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp:12629)

**② 推断与改动边界**

- **高置信度**：现状足以解释附近碎触点出墨；尚非硬件根因证明。保留墨迹的Up策略也会保留激活前误写。
- **高置信度**：策略应放Controller消费层，复用上述收尾及隔离；新Down须同时防止普通续接绕过，保留真正手掌续接优先。无需把邻域扫描放入ContactInput/RTS同步回调，也无需新增系统API，符合Win7基线。
- 同帧激活后立即Up、批内顺序、最大尺寸增长、正常提交Z序均须覆盖；不能承诺像素级即时截断。当前“不会中断其他输入”文案须随行为修订。[zh-CN:775](D:/Project/Inkeys/Repo/Inkeys/Inkeys/src/i18n/zh-CN.jsonc:775)

**③ 产品待选**

- 后来进入范围的旧笔：仅激活时截断／随移动持续截断；后者防误写更强，也更容易打断邻近书写。
- 被拒触点离开：保持隔离至抬起／离开后另起笔；后者需新增恢复语义，产生分段。
- 作用对象：仅Touch／包含笔鼠标；是否豁免其他已激活手掌，决定多人擦除是否互扰。
- 断触预约期：继续拒写／仅物理接触时拒写；前者防碎点，后者减少空挡误拦。

验证接缝可复用 [palm_eraser_tests.cpp:295、325](D:/Project/Inkeys/Repo/Inkeys/InkeysHeadlessTests/palm_eraser_tests.cpp:295) 和生产 `--draw3-palm-eraser-test` 的转换、邻域、续接、Laser收尾用例（C:5542）。需补“保留历史、隔离Move、真实Up回收、续接不复活”的组合验证；现有probe不证明完整调度、GPU或实机行为。本轮未执行测试。