# 运行时实施交接

## 范围

子代理仅修改运行时与相关测试；Setting/i18n、最终 spec、构建和运行验证由 root 负责。产品源码已冻结交 root 统一构建。没有执行 commit/push、GUI、hidden-window 或任何 EXE；未自行启动 MSBuild。

## 实现

- `Other.Config.cppm`：新增 PPTHelper.IdleFadeBottomPair / IdleFadeMiddlePair / IdleFadeMainBar，NoUpload，默认 true/true/false。原通用 bool codec 已严格要求 Json::boolValue，无需修改解码器。
- `Ppt.cpp`、`PageControl.cppm/.cpp`：三个范围进入同一发布快照和等价比较；ReadPptIdleFadeSnapshot 从已接受状态返回窄值快照。只有场次/可见性/时长/主栏开关变化请求主栏；页码发布不改变主栏计时。
- `Bar.ThemeMaterial.h`：唯一固定背景 0.30、内容 0.40 上限及 ResolveIdleOpacity(normal,ceiling,progress)，只减不增。
- `Bar.Rendering.cppm/.cpp`：局部 residentIdleFade 只在常驻绘制段生效。Shape 的正常材质先 cap，再组合 MainBar.pct/0.8 收展可见因子；Superellipse 主按钮先按当前正常材质（含折叠端点）cap。SVG/文字/PNG 内容沿现有常驻内容通道消费一次正常值。
- `Bar.Scene.cppm/.cpp`：独立闲置进度，SetOpacity 仅显隐；分页根底板和常驻内容应用上限，菜单/主题面板绘制在局部作用域之外。backgroundOnly 的 Render 和 Geometry 均屏蔽闲置，防复用泄漏。
- `Bar.Interaction.cpp`、`Bar.Main.cppm`：已准入输入发布轻量活动/悬停/按下，Leave 保留仍有效的拖动按下，Up/Cancel/Finish 清理。FreezeMainPresentedButtonHit 在原同 serial 循环保存 mainIdleHit，Down 使用冻结结果，避免业务接受却未恢复；Move/Up 用稳定当前成功几何，主按钮采用 Grip 映射。接受的 Raw 包在 screen→layout 后、Rigid 变换前同源观察。
- 主栏 render owner 使用 PptMainIdleFadeState 独立持有生命周期/保护边沿基准，externalTurn 永远 false。画笔/形状/橡皮/撤回/更多及可交互子面板、实际退场进度聚合保护；最后结束后完整重计时。
- 主栏复用 PageLoadingDeadline，经 PumpSharedInteraction 合并下一期限；到期 CAS 单次消费请求 Bar，无新线程或轮询。RenderLoop 局部应用主栏/主按钮后恢复 renderer 参数。场景退出/new session 直接清零进度时显式贡献 dirty/needRendering。
- PageControl 同一可见场次只是重勾选/从关闭开启时只 MarkIdleActivity，保留实时 hover/press 并消费旧外部序号；真正场景/场次进入才 ResetIdleFade。主栏配置取消也不会清掉仍有效的实时悬停。

## 已新增的验证（尚待 root 编译/运行）

- `page_control_tests.cpp`：主栏独立超时、任意长时间面板/交互保护、最后关闭/离开重计时、重复快照不重置、开关/新场次/白板边界、三个范围快照比较与默认。
- `bar_theme_tests.cpp`：上限低/等/高值、动画中间值，50%内容到40%而非20%，30%内容不加深。
- `IdtMain.cpp` 既有 `--config-boundary-test`：三 bool 八组合读写、ReadAll/ReadMini、缺字段/非法类型，以及原等待/正常透明度保持。
- `PageControl::RunOffscreenTests`（由 `--bar-eraser-offscreen-test` 调用）：真实 Scene/D2D 根背景约30% alpha、40%内容叠加后约58% alpha、恢复像素、命中、同帧反向、关闭动画和 backgroundOnly 复用。
- `Bar.EraserAttribute.Test.cpp`：生产 Shape 浅/深主题，主栏 pct=.8/.6/.4/.2/.0 的真实中间帧像素，证明背景 cap 后仍乘完整收展可见因子；p=0 正常可见关系保持。
- `--ui3-input-worker-test`：真实 mailbox 冻结 Down 后提交新几何再消费仍恢复；Leave/drag press、Finish 清理、主栏期限进入真实 PumpSharedInteraction 等待且只消费一次。为验证成功提交边界，现有测试函数作为 BarUISetClass friend，不新增测试旁路或窗口。

## 静态检查与交接

- `git diff --check --ignore-submodules` 通过，只有 Git autocrlf 对 PageControl.cpp 的 LF 预警。
- 修改前字节备份位于忽略目录 `Build/ppt-idle-runtime/backups`，16 个运行时/测试文件检查 BOM/EOL；PageControl.cpp 原始全部 LF，仍为 LF，其余按原 CRLF/BOM 保留。
- 文件列表：`Build/ppt-idle-runtime/changed-files.json`。
- root 独占完整 Debug|ARM64 Solution 和所有测试进程，避免测试锁住链接目标。实际构建/运行结果以 root 最终记录为准，不把本报告当作通过证据。

## 已明确移交的最后核对点

- checker 冻结后建议 mainIdleHit 额外包含 `!moreHit && event.mainButtonHit != nullptr`，覆盖动画中超出根底板但仍受理的真实主栏按钮。已通知 root 在其构建前做该一行增量；子代理保持源码冻结。
- direct reset 的单独 presentationVisible 变化、无其他布局变化的完整 RenderLoop 回归未专门新增；源码已显式贡献 dirty，已向 root 如实说明。现有真实生产绘制/输入/像素测试不等于完整桌面合成或 Office 现场验收。
