# C1 实施计划

本轮交付为本文与 design.md。以下步骤供设计审定后的实现阶段使用。步骤中的文件名均对应下面的完整路径。

## 实现阶段源码改动文件白名单

| 文件 | 必要修改 |
| --- | --- |
| Inkeys/Inkeys/Other/Other.Config.cppm | UserDivider ID/默认尺寸、三段值结构、有序旧字段映射、配置整组快照/替换与导入标志访问的声明。 |
| Inkeys/Inkeys/Other/Other.Config.cpp | rwMutex 下整组取值和替换；TryMigrateLegacyBarButtonLayout 中识别旧数组的精确 A1 顺序。保留现有元素/数组 codec。 |
| Inkeys/Inkeys/UI/Bar/Bar.LayoutModel.cppm（新） | 导出四段值、目录、规范化、序列化、编辑决策、纯导入与重置。 |
| Inkeys/Inkeys/UI/Bar/Bar.Button.cppm | 公开模型读取/应用、启动准备、reload 消费接口；实例拥有者、待保存/加载状态；删除旧活动栈声明。接口分区显式 import 新模块，供使用模型的调用方直接 import 叶模块。 |
| Inkeys/Inkeys/UI/Bar/Bar.Button.cpp | 注册用户线、16 项可重复、独立实例与投影；按三段 Load，发布 More、本地化与设备缓存遍历；实现布局应用；设置 Main 装箱参数。 |
| Inkeys/Inkeys/UI/Bar/Bar.Initialization.cpp | 注册后、首次 Load 前调用一次启动规范化/旧标志导入。 |
| Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp | RenderFrame 消费 reload 请求，保持 More 的现有空第二组装箱路径。 |
| Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp | 替换可删除对象跨消息的裸身份，校验手势提交时原实例在位。 |
| Inkeys/Inkeys/UI/UI3/UI3.ListLayout.cppm | ListLayoutItem 增加默认关闭的独占窄列标记。 |
| Inkeys/Inkeys/UI/UI3/UI3.ListLayout.cpp | 大档 PairedRows 对该标记封列并推进，保留原边界与单行行为。 |
| Inkeys/Inkeys/UI/Setting/Setting.cpp | 16 个旧控件绑定布局存在性，调用 AutoAdd/RemoveAllOf/应用入口。 |
| Inkeys/Inkeys.vcxproj | 产品编译新叶模块。 |
| Inkeys/Inkeys.vcxproj.filters | 新模块放在 Bar.MainLayoutPolicy.cppm 同目录 filter。 |
| InkeysHeadlessTests/InkeysHeadlessTests.vcxproj | ClCompile 同一产品叶模块，并编入新的 headless 测试翻译单元。 |
| InkeysHeadlessTests/bar_layout_model_tests.cpp（新） | 纯模型、编辑与有序导入测试。 |
| InkeysHeadlessTests/animation_tests.cpp | 声明并累计新测试返回的失败数。 |
| InkeysHeadlessTests/config_metadata_tests.cpp | 复用隔离配置夹具测试真实 codec、旧数组迁移、三段事务及重读。 |
| InkeysHeadlessTests/ui3_list_layout_tests.cpp | 两档装箱新增相邻用户线与固定线的坐标断言。 |
| InkeysHeadlessTests/recall_attribute_tests.cpp | 更新检查 Load 函数范围的结束锚点，保留原投影要求。 |
| Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp | 真实按钮矩阵改用持久化 B，增加实例、More、刷新/丢设备、身份与连续线的离屏用例。 |

实现验收阶段另有唯一配套规范文件 .trellis/spec/native-desktop/configuration-i18n-and-assets.md，仅更新 design.md §10 指定合同。工程及 filters 保持原格式；新建或修改 C++ 单元 UTF-8 BOM、CRLF。两份任务文档使用无 BOM UTF-8、LF。没有新 UI 文案、翻译、图标、资源、依赖、工具链或 solution 编辑。

## 有序步骤、验证与回滚点

1. **取得比较基线。** 以当前工作树的实际输入构建 solution（命令见下），记下配置、字体、渲染设备、测试退出码及既有 SKIP。用独立临时目录保存 --ui3-golden-dump 的清单、状态、.bgra、.png；私有测试配置留存旧版样本和全新样本。验证输出能逐文件读回并与场景清单对应。回滚点为原有源文件与只读基线；后续输出写另一目录。

2. **配置值与纯模型。** 按 design.md §2、§3、§6 实现新 ID、值结构、FromConfig/ToConfig 和编辑/导入纯函数，有序映射以既有 16 个稳定 ID 和同顺序的旧字段为唯一来源。将精确旧 A1 迁移放在旧数组拆分末尾，移除 Load 中同条件的旧行为时与运行时切换合并。先登记两个工程及产品 filters，测试入口加 RunBarLayoutModelTests；产品注册元数据通过只读快照传入，避免引入 Bar 分区反向依赖。验证 H1–H5 的纯值用例、旧文件迁移与新顺序对照、重复/缺席 ID 往返。回滚点为叶模块及登记/测试整体，旧消费者可继续走旧路径。

3. **装箱。** UI3 的窄列标记放在结构末尾以保留已有 aggregate 初始化；仅在 Main 且精确 UserDivider 时选择新分支。实际细线绘制与命中排除沿用 Divider preset。验证 H6，逐项对照现有单行、双行与 DenseRows 测试，另检查半列收尾、紧贴固定线、多个用户线及两个方向。回滚点为 UI3 两文件、Bar 的参数分支与新增坐标用例，不改固定线参数。

4. **事务、实例及真实栏切换。** 在 Config 内添加布局值批量读写，保证与 Write()/Config 复制互斥；普通快照不读旧字段，仅启动读取标志，重置路径显式清零。Bar 初始化/应用共用布局提交锁。注册原型、注册顺序、资源加载失败的 ID 保留与 startupReady 一起落实。用 FromConfig 替换 Load 的本地校验及旧投影；稳定复用单例和未变布局的重复项，发布两条交界线及单个 explicitMore 组。投影/刷新文案/设备丢弃对实际副本逐一生效，修复跨消息 hover、双击与已删按钮手势生命周期。缺少资源的插件仅保存在配置中。验证 O1、O2 中的身份/局部状态/缓存场景、H5 私有文件重启和失败重试；验证默认启动不写盘，导入/修正同一启动只保存一次。回滚按一个单元撤回注册、Load、启动和所有消费者的切换，复测时恢复私有旧配置副本，避免拿已清零的开关验证旧版。

5. **接入编辑与旧控件。** 实现 ApplyBarLayout 与 pending 请求，设置提交完成后只通过 UpdateRendering(false) 唤醒；RenderFrame 消费后在 owner 更新。设置页 16 行统一改为布局存在性绑定。将旧同步包装的最后调用点、测试夹具与源码检查锚点在同次切换中替换。第 4、5 步作为同一可运行检查点交付。验证 0→1→多项→删除、含 More 的全删、Setting 唯一与连续提交；不跳过状态矩阵或等待模拟器替代真实帧。回滚点为第 4、5 步接线整体，保留已可独测的叶模块和装箱分支。

6. **集成验证。** 在当前 VS 安装中定位 ARM64 原生 MSBuild 的完整路径，用同一 PowerShell invocation 先执行 Remove-Item Env:PATH -ErrorAction SilentlyContinue 和 $env:MSBUILDDISABLENODEREUSE='1'，再调用该绝对路径，参数 InkeysRepo.sln /p:Configuration=Debug /p:Platform=ARM64 /m；超时至少 5 分钟。检查退出码与首个有意义错误。InkeysRepo.sln:12、45、53–66 已声明测试目标及 ARM64 映射。运行本次产物 InkeysHeadlessTests.exe --no-window，以及 Inkeys.exe 的 --config-boundary-test、--bar-eraser-offscreen-test、--ui3-geometry-attribute-test、--ui3-host-offscreen-test、--ui3-attribute-dirty-test。CLI 见 IdtMain.cpp:1908–1933、2061–2071。记录每个退出码，核对 design.md §9 每项及旧断言对照；测试产物与实际用户配置隔离。回滚到最近通过的配置、装箱或接线检查点定位，不放宽断言。

7. **像素、重启与合同验收。** 同环境重跑 --ui3-golden-dump 到新目录，要求基线全部 OK 场景仍 OK，场景集合、尺寸、BGRA 及 PNG 全部逐字节比较通过。私有配置写 B1 四组件与两条相邻线、More 含重复项；新进程重读并通过真实离屏 Load 检查顺序、位置和状态。真实窗口/重启观感由有 GUI 授权的主 Agent 或用户执行。按 design.md §10 更新 spec，再检查 git diff --check、白名单、编码/换行、无无关 diff。发现差异保留两份输出，按同一场景回查源模型、实例投影或布局；回滚到对应步骤，禁止覆盖基线。以上回滚点均指受控撤回本阶段改动，无需创建提交或执行 Git 写操作。
