# 技术设计

## 边界和接口
- StateModeSelectEnum 末尾追加 IdtClickThrough，保留既有值。新增 IsComputerInteractionMode(StateModeSelectEnum)、ChangeStateModeToClickThrough()、SetSelectAndClickThroughMode(int)、ChangeStateModeToSelectionFromButton()、ToggleClickThroughFromButton()、ToggleFreezeFromButton()。
- 使用 IdtState 现有状态锁/修订。lastAnnotationMode只保存最近画笔/图形/橡皮顶层模式，笔型/属性保存在现有状态；显式进入穿透的程序入口保持幂等；用户穿透按钮切换进入/退出，退出恢复该记忆且不恢复旧定格。定格从穿透恢复该记忆。
- ProductState.selectionMode 由电脑操作模式判断投影（Selection或ClickThrough），复用Draw3和辅助ULW，不改后端工具枚举。
- UI.Bar.SelectAndClickThroughMode 通过新schema IdtAtomic<int>保存0/1。读取规范化。SetSelectAndClickThroughMode更新字段与工具映射，调用SyncDraw3State和主栏刷新；由Setting队列负责写盘，函数本身不写盘。
- 分离式选择按钮先发布现有Clear，Accepted才切Selection；空页直接切换。系统PPT可信退出的条件Selection入口保持现有语义，不能让通用Selection入口自动清空全部系统调用。
- 输入滚轮、设置owner、状态呈现和桥接测试统一使用电脑操作判定；主栏仍分别高亮Select/Pierce。

## 主栏
- BarButtonPresetEnum追加Pierce；shared_ptr持有运行时穿透按钮，Load中紧邻Freeze之前固定加入，后续显隐只更新hide/size，不从运行时按钮列表抽离或重新插入。不改变FixedButtonsA2和旧Pierce退休codec。
- A2纯投影按操作模式、实际工具、当前页内容、Freeze可用性及白板得到freezeVisible/freezeTwoTwo/pierceVisible/pierceTwoTwo；组合进入缓存key。保持已有Whiteboard/EndShow分支。
- Select根据分离式和工具切换i18n文案；清空Disable仅用于空页Selection（注意已有Accepted Clear双击延续）。
- 使用barPierce.svg。英文Select两行 Select/(Clear)；Click-through两行 Click-/through，针对37DIP紧凑槽局部字号调整；DWrite测量不截断。不修改全局网格或其他按钮。

## 归属/协作
核心实现代理拥有IdtState、MouseHook、Bar全体和模式/布局测试。
设置代理拥有Other.Config、Setting.cpp、三语言jsonc、生成头/快照和config_metadata_tests。
主会话负责任务/规范、最终构建和验证；检查代理负责全范围检查与修复。
不改COM ABI、历史Draw2/IdtFloating、第三方或其他任务。
