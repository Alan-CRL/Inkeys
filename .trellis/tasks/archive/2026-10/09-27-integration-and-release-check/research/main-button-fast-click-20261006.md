# Research: 主按钮快速点击的对照场景差异

- Query: 2026-10-06 当前F81F Release与精确0811 Canary的七次注入中，为何150ms反向后终态不同；是否应修产品。
- Scope: internal；仅主按钮创建、消息归一化、业务门控与既有合同。未运行GUI、构建或测试，未修改源码。
- Date: 2026-10-06

## Findings

1. **有直接源码依据的首要原因是既有300ms业务合并，而不是CS_DBLCLKS。** `Inkeys/Inkeys/Window/Window.cppm:94` 的默认 `classStyle=CS_HREDRAW|CS_VREDRAW`；`Inkeys/IdtMain.cpp:2890–2925` Bar创建分支没有覆写classStyle，只有坐标固化callback；`Inkeys/Inkeys/Window/Window.cpp:1026` 注册此style。因此本Bar正常创建没有CS_DBLCLKS，不能把第二击假定为WM_LBUTTONDBLCLK。
2. `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp:3824` 主按钮接受WM_LBUTTONDOWN；Seek完成并allowClick后，`:3843` 先调用 `TryBeginToggle(BarToggleChannel::Main)`，只有成功才在`:3852/:3858` 更改fold并增加点击pulse。`Bar.Main.cppm:420–422` 委托现有coalescer；`Bar.ToggleClickCoalescer.cppm:86–103` 默认300ms，拒绝距离同通道上次成功动作不足300ms的toggle。5000ms与5150ms的150ms间隔，扣除相近35ms按压等待仍明显在窗内。正常路径因此预期六次有效toggle，初始collapsed→最终collapsed，与Root真实观测吻合。没有原始业务日志逐击证明每个动作，故这是强源码归因，不能把全部13/37图像变化单独归到门控。
3. **这是被明确规定的交互合同。** `.trellis/spec/native-desktop/rendering-and-ui.md:1394–1402`《UI3展开按钮点击合并合同》要求第一次立即执行、299ms合并、300ms边界通过、通道独立；保留原始输入，只合并重复显隐动作。当前门控位置符合该合同。不得为了CPU对照绕开门控、减窗或删除Main通道，这会改变既定交互语义。
4. 只读查看本地已有旧commit `82f7b7c02080c253661514d31b55f3a827382e1d` 的相同 `Bar.Interaction.cpp`：旧主按钮Seek距离判断后直接更改fold，没有TryBeginToggle；旧commit中不存在 `Bar.ToggleClickCoalescer.cppm`。这支持新旧场景工作量不同，但旧source与精确EXE的build指纹未由本研究重新证明，不能声称二者完全同源。
5. 其它主栏按钮路径 `Bar.Interaction.cpp:3913–3919` 明确接受DBLCLK continuation，但这不能反推出Bar本次原生窗口已启用CS_DBLCLKS。主按钮DOWN-only属于可另行讨论的未来classstyle变化风险，本次不构成确认缺陷。

## Recommended bounded next step

- **本场景无确认需要的生产修复。** 保持触摸误双击合同和动画参数；对照输入第二次反向间隔用350ms或更长，并观察是否仍在实际动画途中。每轮开始与结束均保存截图并确认fold；不要用“全部SendInput成功”代替“全部业务toggle成功”。
- 若350ms已晚于实际动画完成，普通共同toggle作为等工作量CPU对照；150ms快速输入另外作为现版合并合同功能检查。不要改变动画时长或关效果强造共同场景。真正动画中反向对照须找到双方都能接受且尚在动画中的合法间隔，否则明确语义不等价。
- 当前150ms三轮CPU以及ROI变化不可继续计算同等工作量性能提升；保留原始样本标记场景不等价。重新3轮同场景预算足够，不扩大性能工程。

## Files found / Related specs

- `Bar.ToggleClickCoalescer.cppm`：单调时钟按通道300ms显隐业务门控。
- `Bar.Main.cppm`：BarUISet统一业务入口，coalescer成员所有权。
- `Bar.Interaction.cpp`：坐标固化、主按钮Seek、Main toggle与渲染唤醒。
- `IdtMain.cpp` / `Window.cppm` / `Window.cpp`：Bar创建style与窗口服务注册。
- `.trellis/spec/native-desktop/rendering-and-ui.md`：业务合并、动画当前/目标值、共享调度器线程归属合同。
- `.trellis/tasks/09-27-integration-and-release-check/{prd,design,implement}.md`：有限最终验收，不解除已有语义换取数值。

## External references

未进行外部查找。本结论基于本仓库注册style、明确业务代码和spec，不新增平台API主张。

## Caveats / Not Found

本研究不运行测试；未获取逐条原生消息/业务接受时刻，不能声称逐击日志证实。精确0811旧EXE与旧commit内容关系沿用Root提供依据。Win7/Draw3及其它脏文件未检查或修改。没有加载implement.jsonl/check.jsonl（research角色隔离）。所有结果仅落盘本task/research。
## 补查：合法动画中反向的刺激参数

- 现版 `Bar.Metrics.cppm:25` 的 `BarButtonDefaultOperationDurationSeconds=0.4`，由 `Bar.Animation.cpp:20–21` 初始化默认操作时长；全仓精确赋值搜索只有这一处。`Bar.RenderLoop.cpp:1337` 主按钮pulse/形状使用该时长，`:1458` 主栏也取此时长，`:2415` 新fold批次 `mainBarTimeline.Restart(operationDur)`。旧82f7 `Bar.Animation.cpp:19` 同为0.4秒。SpeedRate=1下，**300ms门控到400ms动画末端之间存在约100ms合法共同窗口**，所以350ms反向名义上仍位于动画后段，不能断言共同Click链完全无法中途反向。
- 推荐保留22秒、7击、其它参数不变，仅 `5150→5350ms`，即 `1000,3000,5000,5350,7000,9000,20000ms`。不选恰好300ms边界，以免35ms按压/Seek交互线程延迟使两个实际成功toggle间隔略低于阈值；350ms留50ms裕量。主按钮Seek松手后才toggle，35ms保持两击同样规则，不更改helper或动画配置。
- 起点必须collapsed圆，末点预期expanded方；七次全部业务接受才满足该奇偶端点。每轮prep独立一次点击并至少2秒静置恢复collapsed，保存first/last实际截图；不能由上一轮末点推断下一轮起点。若5350仍被门控/坐标变化拒绝或末点不符，本次有限3轮立即标协议失败，不用CPU数据宣告收益，也不重复试不同间隔。短ROI捕获在5000至5350之间存在图像变化仅支持可见变化，不能单靠它证明5350时动画尚未结束；真实动画反向的精确进度仍需实际事务/连续图像证据。