# 设置、配置与无窗口回放

## 已实现
- `UI.Bar.IconOrder` 为 `IdtAtomic<int>`，默认0；`UI.Bar.ExpandDirectionAdjustment` 默认1，均为NoUpload。生产共享读路径严格检查JSON整数类型、`isInt()`和合法范围；ReadAll/ReadMini沿schema默认恢复缺失字段，超宽整数不会窄化成合法值。
- 常规页选择与穿透下方、自启区域前增加两个60DIP卡片，卡片间5DIP间距。使用现有背景/边框/圆角/字体/ComboClass，按三语言最长选项测量宽度。
- 只在真实选项变化时赋值原子配置，然后`barUISet.UpdateRendering(false)`唤醒布局，`QueueConfigWrite()`冻结快照入现有FIFO；没有调用工具setter或Draw3同步。
- 下拉弹窗局部使用8×4DIP内边距，避免继承卡片零内边距后英文末行下伸字形贴边；颜色/控件样式保持现有实现。
- 三语言键及生成头/基准snapshot已同步，未改动原有模式文案。
- 真实config_metadata_tests加入全合法值读写/重启/Mini隔离、缺失与非法值/类型/大整数、reset、冻结副本及NoUpload断言。缺失与非法测试先设为非默认值，确保确实覆盖恢复默认路径。

## 已运行检查
- `Scripts/i18n.ps1 sync` + `check`：en-US和zh-TW均357/357、100%、退出0。
- 生产UI块+ComboClass无HWND回放：`replay-toolbar-settings.py`生成忽略目录`Build/ToolbarOrderExpansionValidation/settings-replay.cpp`，使用当前VS安装`vcvarsall.bat arm64`和原生cl编译；真实imgui源码与仓库SC/TC/Fluent Icons/icomoon字体，字体顺序与既有回放一致。
- 三语言×0.6/1.0/1.5/2.0共12组退出0；每组writes=6、wakes=6、clipped=0、glyphClipped=0、missing=0、overlaps=0、optionChecks=160、previewChecks=172、failures=0。
- 四种图标顺序轮换0→1→2→3→0，方向1→0→1；每次变化保存并唤醒一次，重复选择不保存不唤醒。冻结fixture序列为(1,1),(2,1),(3,1),(0,1),(0,0),(0,1)。
- 下拉初始隐藏测量帧不判定呈现裁切；可见帧按照真实字形包络测量，包含末行英文g等下伸字形。初版检查发现零popup内边距裁切约1px，局部修正后12组全部通过。

## 验证边界
回放的原子字段、渲染唤醒、QueueConfigWrite仅为计数fixture，不能证明实际生产FIFO线程同步或HWND呈现；真实Config测试需主会话完整solution/headless执行。真实窗口三语/系统DPI及交互仍需人工验收。本代理没有启动GUI、使用Computer Use、commit/push或结束任务。

## 完整构建后的修正与复测
第二次solution编译发现新增块未限定config，已改成Inkeys::config；回放fixture改为只在namespace Inkeys声明并增加生产限定名断言，重生成并用原生ARM64 cl编译后再次运行12组，退出0，各计数/裁切/缺字/重叠结果与上表一致。使用仓库根WorkingDirectory解析既有字体相对路径。最终solution编译退出0，生产作用域由真实编译验证。
