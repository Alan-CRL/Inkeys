## 配置与迁移

- 【事实】`UI.Bar.FixedButtonsA1`、`ExtensionButtons`、`FixedButtonsA2` 均为 `ConfigSequence`。A 项为 `{Id, Size}`，B 多 `Visible`，缺省为 `true`。`Size` 支持 `twoTwo/twoOne/oneTwo/oneOne`。`Snapshot()` 加共享锁，`Replace()` 加独占锁。JSON 整数组解析成功才替换，非法 `Visible` 使 B 保留 schema 默认值 `[MoreBoundary, Setting]`。旧 `ButtonLayout` 仅在三区字段均缺失时拆分，剔除 Divider 和已退役 Pierce。（`Config.cppm:98–172,256–261,417–419`；`Config.cpp:337–419,454–486,1148–1166,1171–1249`）
- 【事实】`Load()` 校验并写回 A1/A2。允许区内重排，成员必须恰好是该区默认集合；剥离 Divider，忽略 `Visible`，尺寸按注册默认纠正。旧 A2 双项顺序保留并补 `EndShow`。A1 的精确旧顺序 `Select, Draw, Eraser, Geometry, Recall, Clean` 会被迁回新默认，编辑器若允许该排列，须把这条迁移改为一次性。（`Config.cpp:349–378`；`Button.cpp:1362–1429,1576–1596,1676–1688`）
- 【事实】B 的 codec 已保序并接受重复。当前 `Load()` 完全跳过 B；未接入的 `NormalizeExtensionZone()` 保序、保留 `Visible` 和合法未知扩展 ID，已注册单例只取首项。`MoreBoundary` 最多一个，无实体，规范为 `twoTwo/true`，缺失时不补。合同定义标记前最多两个可见组件上栏，其余为 `forcedOverflow`，标记后为 `explicitMore`；当前实际显式组只有注入的 `Setting`。相邻 Divider 折叠代码在 `1459–1465`，但官方 Divider 注册在 `FixedA1`，进 B 会先在 `1445` 被过滤。（`Button.cpp:174–194,769–775,1432–1486,1622–1648`；`合同:266–275`）
- 【事实】本分支活动启动链直接初始化 UI3 Bar；源码搜索未找到 `useInkeys3UI` 或 UI2 主栏入口，`LegacyDrawState` 是迁出的残留状态。16 个旧开关仍是 **UI3** 的输入：设置页写开关、排队保存、调用 `SyncLegacyExtensionButtons()` → `Load()`。初次按注册顺序，关掉即移除，再开启追加到进程内栈尾。（`IdtMain.cpp:4134–4144`；`LegacyDrawState.hpp:3–13`；`Setting.cpp:1552–1556,4277–4362`；`Button.cpp:1505–1559,1686–1689`）
- 【推测】只让编辑器写 B，真实栏仍显示旧开关；直接改读 B，旧用户启用的组件可能消失。建议以独立迁移版本一次性导入已开启且 B 中缺失的 ID，按注册顺序放在标记前，保留已有条目、显隐与顺序。随后以 B 为唯一来源。重复项须有独立 UI3 对象和实例身份；额外 Divider 用独立扩展 ID，保留连续项，并为同一间隙内的多条线定义布局，否则坐标重叠。拖入收纳写到标记后，`Setting` 保证只出现一次。（`Button.cpp:144–170,769–799,1432–1486,1505–1559,1622–1648`；`UI3.ListLayout.cpp:97–123`）

## 组件库

【事实】16 项均属 `Extension`，默认 `twoTwo`、`allowMultiple=false`，图标为 `UI` SVG。下表 ID 省略公共前缀 `Component.ShortcutButton.`，SVG 省略 `comp`。分类和标题来自 `SettingsUI.Component.*`，现有文案只有部分依赖说明，缺少通用卡片副标题。（`Button.cpp:803–864`；`Inkeys.rc:474–504`；`zh-CN.jsonc:1267–1296`）

| 现有分类 | ID 后缀／设置标题／SVG 后缀 |
|---|---|
| 软件 | `Appliance.Explorer`／启动文件资源管理器／`Explorer`；`Appliance.Taskmgr`／启动任务管理器／`TaskManager`；`Appliance.Control`／启动控制面板／`ControlPanel`。（`Button.cpp:867–884`；`zh-CN.jsonc:1270–1277`） |
| 系统 | `System.Desktop`／显示桌面／`ShowDesktop`；`System.LockWorkStation`／锁屏／`LockScreen`。（`Button.cpp:886–897`；`zh-CN.jsonc:1271–1279`） |
| 键盘模拟 | `Keyboard.Keyboardesc`／按下 ESC／`Escape`；`Keyboard.KeyboardAltF4`／按下 Alt+F4／`AltF4`。（`Button.cpp:899–910`；`zh-CN.jsonc:1272–1281`） |
| 点名器 | `RollCall.IslandCaller1`／IslandCaller 1 随机点名／`RollCallRandom`；`RollCall.IslandCaller2`／IslandCaller 2 单人点名／`RollCallSingle`。两项均需 IslandCaller 插件和 ClassIsland URL 协议。（`Button.cpp:912–923`；`zh-CN.jsonc:1282–1291`） |
| 点名器 | `RollCall.SecRandom1`／SecRandom 1 闪抽／`RollCallQuick`（URL）；`RollCall.SecRandom2`／SecRandom 2 闪抽／`RollCallQuick2`（IPC）；`RollCall.SecRandom2Compat`／SecRandom 2 闪抽（兼容）／`RollCallQuickCompat`（URL）；`RollCall.NamePicker`／NamePicker 随机点名／`NamePicker`（URL）。（`Button.cpp:924–947`；`zh-CN.jsonc:1284–1294`） |
| ClassIsland 联动 | `Linkage.ClassislandSettings`／应用设置／`ClassSettings`；`Linkage.ClassislandProfile`／档案编辑／`ClassProfile`；`Linkage.ClassislandClassswap`／快速换课／`ClassSwap`。三项标题均带 `ClassIsland`，共用 URL 导航协议说明，换课还需已加载课表。（`Button.cpp:949–966`；`zh-CN.jsonc:1288–1296`） |

【推测】“全部”汇总各页；“桌面控制”合并软件、系统、键盘模拟；“点名器”收六项；“ClassIsland”收联动三项；“内置”收官方项和可添加的 Divider。依赖说明沿用现有键，卡片副标题另补用途文案。（`Button.cpp:762–775,867–966`；`Setting.cpp:4297–4362`）

## 导航、重置与预览

- 【事实】组件页是 `Components=9`。两个目标都在“窗口与显示器 → 窗口”，即 `Window=18/WindowLayout`。尺寸字段 `Experimental.Inkeys3.UI3.SizeMode` 为 `0` 大、`1` 小；图标顺序 `UI.Bar.IconOrder` 为 `0` 跟随、`1` 反向跟随、`2` 固定左→右、`3` 固定右→左；`ExpandDirectionAdjustment` 单独控制展开侧策略。首页已有 `navigateTo({page, section})` 示例，路由只含页和子页，行级滚动需另接。（`Setting.Navigation.h:13–35,57–61,69–79`；`Setting.Pages.cpp:1489–1500,1597–1611`；`Setting.cpp:2463–2475,2911–2917,3581–3592,3678–3683`；`Bar.MainLayoutPolicy.cppm:9–33`）
- 【事实】性能页 `Performance=16` 当前只有帧率与渲染设置，没有二次确认重置。可复用 `Design::ButtonRow` 的稳定按钮 ID，确认状态须新增；`SliderResetAction` 单击即触发。新装布局为 A1 `Select, Draw, Geometry, Eraser, Recall, Clean`（全 `twoTwo`），B `[MoreBoundary, Setting]`，A2 `Whiteboard/twoOne, Freeze/twoOne, EndShow/twoTwo`。16 个旧开关均为 `false`；保留旧投影期间，重置也须清这些开关才与新装一致。（`Setting.cpp:4644–4691`；`Setting.Controls.cpp:1303–1319,2404–2415`；`Config.cppm:244–271,417–445`；`Button.cpp:1505–1559`）
- 【事实】展开栏恒注入 More、两条交界线，More 恒含 Setting。大档高 80 DIP，Divider 为 `oneTwo`、纯 Shape `1×50`、占已有 5 DIP 间隙；小档高 42.5 DIP，普通按钮投影为 `oneOne`，Divider 在同一间隙内高 28.5 DIP。More 先装显式组、再装溢出组，溢出靠栏，两组都有内容才显示横线；大档最多五标准列，小档最多十小格列。现有悬停提示在拖动时停用。（`Button.cpp:233–259,1090–1103,1622–1648,2075–2156`；`Bar.Metrics.cppm:24,78–106`；`UI3.ListLayout.cpp:97–123`；`Bar.RenderLoop.cpp:5492–5508,5625–5657,8087–8108`）
- 【推测】预览复用 UI3 布局与 More 动画，以独立实例接管拖放，避免执行真实组件动作。两档按同一目标高度缩放，再用可用宽度限幅；收纳显示实际两组，拖拽提示单独驱动。【待确认】实测触控经过 More 时的展开、跨区插入和连续 Divider 命中。（`Button.cpp:841–860,2039–2156`；`Bar.RenderLoop.cpp:5482–5493,8087–8108`；`UI3.ListLayout.cpp:97–123`）
- 【事实】`setting_design_tests.cpp:4831–4833,7921–7974` 锁定导航顺序并检查旧组件 `ToggleRow` 的多语言排版；`config_metadata_tests.cpp:154–160` 检查 B 序列复制；`recall_attribute_tests.cpp:433–437` 依赖 `SyncLegacyExtensionButtons` 函数名作为源码断言边界。应用离屏测试覆盖开关同步、最多两项上栏、显式组恒有 Setting 和溢出数量；多目标测试依赖 More 实例稳定。（`Bar.EraserAttribute.Test.cpp:694–714,866–922`；`IdtMain.cpp:2069–2071`；`Bar.MultiTarget.Test.cpp:263–276`）本次仅静态核查，未修改文件，未运行测试。

### 文件索引

`Config.*` → `Inkeys/Inkeys/Other/Other.Config.*`；`Button.cpp` → `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`；其他 `Bar.*` 同目录；`Setting.*` → `Inkeys/Inkeys/UI/Setting/`；`UI3.ListLayout.cpp` → `Inkeys/Inkeys/UI/UI3/`；`IdtMain.cpp`、`Inkeys.rc` → `Inkeys/`；`zh-CN.jsonc` → `Inkeys/src/i18n/`；三个小写 `*_tests.cpp` → `InkeysHeadlessTests/`；`合同` → `.trellis/spec/native-desktop/configuration-i18n-and-assets.md`。

## 产品待选

- 【待确认】主栏容量：保留两个 B 按钮，或按可用宽度扩容。建议保留两个，Divider 不计入按钮容量。（`Button.cpp:1622–1629,2082–2100`）
- 【待确认】收纳编辑：展开两组且允许组内排序，或只提供拖入入口。建议展开两组；拖入显式组，自动溢出仍由主栏顺序决定。（`合同:266–269`；`Bar.RenderLoop.cpp:5492–5508`）
- 【待确认】连续 Divider：同一间隙内并排细线，或给额外 Divider 独立宽度。建议前者；固定交界线保持现有间隙规则。（`Button.cpp:2082–2100`；`UI3.ListLayout.cpp:107–123`）
- 【待确认】重置范围：恢复三区及仍参与投影的旧开关，或同时重置尺寸与方向。建议前者，保留用户的显示偏好。（`Config.cppm:244–271,399–400,417–445,590–593`）