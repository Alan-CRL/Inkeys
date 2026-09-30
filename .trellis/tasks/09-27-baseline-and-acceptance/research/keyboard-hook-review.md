# 颜色面板键盘入口独立核对（H0）

只读审查基准：H0 `8b156fca59f0337a6afc6d722941666fcf143080`；未编译、未启动 GUI、未注入按键。当前其他 agent 的改动不作为 H0 结论来源。

## 结论

1. **“H0 打开颜色面板会跨应用吞 WASD/方向键”被当前产品编译链排除。** 唯一调用 `TryQueueColorPickerKeyboardInput` 并在成功时 `return 1` 的低级键盘 hook 位于 `Inkeys/IdtDrawpad.cpp:63–75`，安装处在 `:214–218`。但 `Inkeys/Inkeys.vcxproj:895` 将此文件登记为 `None`；实际编译 `:905` 的 `Inkeys/IdtDrawpadFacade.cpp:42–50` 中，回调只调用 `CallNextHookEx`，`DrawpadInstallHook()` 是空函数。当前第一方源码中没有其他 `WH_KEYBOARD_LL` 安装点。历史设计文档中的“全局 hook 前置颜色面板”描述不再代表 H0 产品执行路径。
2. **颜色面板的方向键/WASD 产品入口存在静态调用链断裂。** `Bar.Interaction.cpp:6779–6805` 保留队列入口，`:2006–2057` 保留 2 DIP 步进和最后 KeyUp 持久化，`:2242–2248` 仍消费 Bar 的 `EM_KEY`；但编译代码中没有调用上述队列入口的键盘 producer。Bar 在 `IdtMain.cpp:1719` 以 `WS_EX_NOACTIVATE` 创建，`Bar.Interaction.cpp:503` 对点击返回 `MA_NOACTIVATE`，`Window.cpp:1400–1402` 对普通 overlay 用 `SW_SHOWNOACTIVATE`；正常点开颜色面板不会让 Bar 获取键盘焦点。故原 `08-01-ui3-simple-color-picker/prd.md` 要求的键盘调整，在普通用户路径上没有可确认的事件来源。该功能缺口是**静态确认，实际设备/焦点行为未运行验证**，归类为中等严重性的输入功能回归；不属于当前可利用的跨应用键盘拦截。
3. **潜伏条件风险仍在导出的队列入口。** `Bar.Interaction.cpp:6782–6789` 对橡皮键检查 `GetForegroundWindow()==floating_window` 或指针命中 Bar；紧接着 `:6792–6805` 对 WASD/方向键只检查按键、存活、Pen 模式、展开的属性栏和 `colorPickerOpen`，没有前台/指针归属检查，尽管注释声称“避免拦截其他应用”。若未来把全局 hook 直接接回旧调用点，颜色面板保持打开、用户切到其他应用时，`Enqueue` 成功会使 hook `return 1`，吞掉该应用的 Down/Up（包括方向键）。这是**条件性设计风险，不是 H0 已触发缺陷**。

## 历史与例外

- `2d1223fe` 为旧 `IdtDrawpad.cpp` 的全局 hook 加入颜色面板调用；`fc9a8b865cd8` 的 Draw3 产品整合把旧文件改为 `None`，加入 facade。现存旧文件、头声明和历史任务的验收勾选均不能证明 hook 仍执行。
- 颜色面板关闭、非 Pen 模式、Bar 折叠、属性栏关闭、`offSignal` 或队列拒绝时，`TryQueueColorPickerKeyboardInput` 返回 false；即使旧 hook 被重新启用也不会因该函数吞对应按键。橡皮键当前有单独的前台/指针门禁；问题限于颜色面板 movement 分支。
- 当前 Bar 若被外部程序强行聚焦，可能收到普通 `WM_KEY*`；这不是正常 `WS_EX_NOACTIVATE` 用户操作，也未通过 GUI 验证，不可据此宣称键盘交互可用。

## 最小修复接口建议（未实施）

先恢复**唯一编译中的键盘 producer**，由其在决定抑制原按键前调用一个生产逻辑判定入口，输入至少包含：按键 Down/Up、颜色面板可用快照、Bar HWND、前台 HWND、指针是否在当前 Bar 表面、该键是否已由当前颜色面板会话接纳。判定在 `TryQueueColorPickerKeyboardInput` 或其紧邻的单一入口实施：只有用户焦点/指针明确归属 Bar 时接纳新 Down；已经接纳的键必须把后续 Up 交还同一会话，即使指针移出或焦点改变，以清除 key mask 并完成一次持久化。仅在成功入队时抑制原事件；关闭/切模式/退出时清理会话。不得把无门禁的旧 hook 直接重新编入，也不要引入第二套颜色状态或同步写盘到 hook 回调。

## 回归证据建议

- 用调用真实生产判定及 Bar 队列/处理逻辑的 headless 测试覆盖：面板关/模式不符/Bar 折叠；面板开但外部窗口前台且指针不在 Bar，WASD/四方向键 Down/Up 均不入队、不抑制；用户操作 Bar 时移动键入队并步进；系统重复 Down 不反复持久化；最后 Up 恰好一次提交；Down 后指针/前台变化仍可闭合 Up；队列满/关闭时不吞键；橡皮快捷键优先级和 PPT 键不回归。
- 静态/构建门禁确认只有一个编译中的键盘 producer 与 hook 生命周期，且真正调用 `TryQueueColorPickerKeyboardInput`；否则纯算法测试无法发现再次失联。GUI 授权后用隔离测试进程检查“颜色面板开→切外部编辑器输入 WASD/方向键→回到 Bar 操作→关闭面板”，分别记录外部应用按键、Bar 颜色变化和 KeyUp 保存。当前仓库规则未授权 GUI，本次未执行。
