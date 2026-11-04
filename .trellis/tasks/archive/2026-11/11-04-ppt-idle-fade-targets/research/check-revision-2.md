# 验收反馈第 2 轮增量静态检查

## 基线、范围与结论

以 `Build/ppt-idle-fade-targets/final-source-state.json` 为上一轮已验证冻结基线，按 SHA-256 定位本轮增量，共 34 个跟踪文件改变或新增。完整增量身份另存 `check-revision-2-snapshot.json`。原第一轮报告中的 40% 终点/独立范围卡片描述是历史检查点，本轮按用户最新要求改为内容 60%、同卡内联/关闭动画折叠及仅底栏模式。

本轮最终静态检查无剩余阻塞代码问题。只读审阅产品与测试源码，没有编译、执行 EXE、GUI 或 Git 写入；修复由 root 执行。

## 发现并确认修复

内联 ConditionalBlock 是当前条件深度的顶层时，其析构会在整个块位于视口上方的情况下同步调整 Scroll、CursorStartPos 与 CursorPos。SettingRow 原先在随后用缓存 origin/bottom 写绝对游标，覆盖这次补偿，造成后续内容锚点漂移。

root 现记录行入口 CursorStartPos.y，仅在 visibilityId 非空的路径将条件块引起的重基准差值加到末尾游标；既有静态内联行不走该修正。修复见 `Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1275`。新增真实滚动宿主、整卡在视口上方、后续锚点在视口内，逐帧关闭和展开的回归见 `InkeysHeadlessTests/setting_design_tests.cpp:3650`；本报告只审阅源码，不声称该回归已经实际通过。

## Controls 条件动画核对

- `PrepareConditionalMotion`（`Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1060`）抽取原 ConditionalBlock 的 generation/target/禁动画初始化，未新建第二套时间轴。
- 预览高度（`Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1209`）与实际 body（`Inkeys/Inkeys/UI/Setting/Setting.Controls.cpp:1265`）均使用 route + PushID(row) + visibilityId 的同一条件 key。预览只设置目标/读取 current，不额外 Advance；真正 parent、frame、height、occupied 仍由原 ConditionalBlock 记录，适配原 ConfigurePageMotion 预推进/高度预测。
- 父行外框使用相同进度的物理像素占位；body 的 fullHeight 明确包含上下 padding，父行 RowGap 保留一次。divider 与 header 高度使用同一绝对像素取整，分数 DPI 下不以两套边界裁剪。
- ConditionalBlock 的 firstIndex/firstVertex 在父卡片背景、标题、说明与 Combo action 绘制后才记录，仅裁剪/合成内联 body；父行/时长下拉不被该条件透明度影响。外层 PageBlock/离页透明度继续走原嵌套合成。
- 退出及 reveal 未完全展开由原 ConditionalBlock 禁交互；关闭 Combo 当帧也由 draft.timeoutIndex 禁用 body 输入。隐藏值仍保留，全部取消合法，真实事件才写回一次配置/通知。
- 首次状态按真实 visible 初始化，反向从已预推进的 current 接续；关闭动画直接归位。反向测试不能要求流逝一帧后高度与上一帧完全相等，现检查中间状态和终点回归，符合原时间推进合同。
- `visibilityId=nullptr` 的旧 InlineRowContent 保持原全高度、渲染和游标路径，不调用新增条件准备；原快捷方式名称内联行不继承折叠动画。

## 底栏门禁与 60% 上限

- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp:7692` 以本帧 BottomDocked 门控，普通/居中均允许，Floating 退休期限并恢复绘制。仅停靠门禁变化不进入 ResetPptIdleInput 的条件，live hover/press 保留；重新进入由原 PptMainIdleFadeState 生命周期重新完整计时。
- `Inkeys/Inkeys/UI/Bar/Bar.ThemeMaterial.h:90` 将唯一内容上限改为 .60，背景仍 .30，解析依然 min(normal,ceiling)；0.8→0.6、0.5→0.5 的纯策略及 Scene 合成 alpha 断言同步，没有改回整体乘法。
- Setting 行已合并时长和三复选项，主栏注明仅底栏模式；移除旧独立范围标题键。20 语言新文案/生成项形状一致。

## 验证边界与后续

- 检查代理静态审阅与格式检查：相对基线 BOM/EOL 结构未见变化；git diff --check 退出 0。原 LF 的 PageControl.cpp 仍会产生 autocrlf 提示，并非本轮格式转换。
- root 正在统一 solution 构建，后续用 UI 专项、原 motion 专项、page-policy 和实际 offscreen 验证本轮新产物。上一轮完整 --no-window 已通过，不由本报告改写或冒充本轮运行。
- 新源码覆盖真实 Combo/checkbox 单次事件、内联中间高度、反向、关闭动画、视口上方滚动锚点；基础算法矩阵未修改，是否实际通过以 root 保存的退出码/日志为准。
- 真实 Office、桌面合成及设备 GUI 观感仍未执行，不能从无窗口检查推导平台或手感结论。

## 最终测试摆位与前提断言补充复核

root 报告本轮产品构建已通过，UI 专项仅新加的“视口上方展开保持锚点”断言失败；诊断为 width=340、scale=1、frame=1、anchor=125（期望64）、scroll=732、occupied=61、reveal=.421。这些是 root 的实际运行证据，检查代理未执行测试。

失败符合原补偿合同：窄列完整内联高度为 2×16 padding + 3×32 控件 + 2×8 间距 = 144 DIP；30ms / 180ms smoothstep 中段单步增量约35 DIP。旧卡片仅在视口上方24 DIP，展开后的新占位可以进入视口，`top+max(current,previous)>ClipMin` 时原逻辑应跳过补偿。收起使用较大的旧占位，因而不受同一边界影响。不能为使此测试通过而修改通用补偿语义。

最终测试只将上方余量24→200 DIP、后续间隔80→300 DIP，使整个动画的旧/新范围都能保持在视口上方，同时后续锚点仍可见。每帧新增的 extent 断言由实际 card.bottom、实际 Scroll 变化和 occupied 差恢复补偿前旧/新最大范围，要求 extent≤ClipMin+1；原锚点误差≤2像素断言保持，未放宽。该前提断言可在合法进入视口时独立失败，不是以锚点结果反证前提。

确认 Setting.Controls 产品源与上轮复核 SHA 完全相同；本次只改变 setting_design_tests.cpp 的摆位/诊断/前提检查，新增 setting-motion.md 的可选内联、CursorStartPos 重基准及逐帧 fixture 前提合同与代码一致。相对原 final-source-state 基线最终增量包括新增专题说明，共35个跟踪文件；当前身份已更新到 check-revision-2-snapshot.json。

本补充静态复核无阻塞问题。root 正在重编译/复跑本轮新产物，尚未在此报告声称最终 UI/专项通过；结果仍以 root 本轮日志和退出码为准。
