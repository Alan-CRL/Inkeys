# 橡皮属性栏与全局粗细控制：交付与验证

## 结果与基线
2026-09-15，主Agent顺序完成，无委派或background Agent。
HEAD仍为`2f87fe69d2f7680eac1e477723937b6ee5bd251c`，分支`feature/eraser`。
任务保持in_progress供实机验收，未提交、推送或归档。旧任务未修改。
原有`Inkeys/PptCOM.dll`逐字节保留；校验见[format-check.md](format-check.md)。

## 已实现的交互与布局
- 主栏首次点击擦除只选工具，第二击展开，再击关闭。真实主栏clickFunc的离屏测试确认首击和紧接的第二击不会被300ms开合合并吞掉。
- 独立视觉面板仍属于同一个Bar HWND/surface、D2D context和RenderPipeline线程，无新Host、设备或轮询线程。
- 高度取MainBar实际高度；标准顺序为清空、16/32/64、自动粗细拆分按钮。按钮/箭头/分割线/留白/预览槽位共同计算宽度；100% UI缩放的正常宽度为433.5 Bar逻辑单位。
- 使用现有primaryBar/mainBar状态、擦除按钮锚点与工作区，左右组镜像且文字不旋转；倒转在淡出中点切换。直接拖动的工作区换算包含同帧窗口位移。菜单方向锁定，父方向变更时重新决定。
- 预览实际像素直径来自DiameterToCanvasPx，除frameZoom放到Bar坐标，再按同一zoom绘制。UI缩放只改变外壳/控件，16/32/64 DIP圆不缩略、不随hover或速度放大。
- 大圆以同心等圆半径绘制，竖直居中，超高只裁上下。正常槽宽至少标准70逻辑单位并容纳预览直径与留白；窄区先减留白，再裁预览中段，保留圆大小。
- 预览与Draw3 HLSL共用EraserGripVisual比例，包含轮廓、白色填充与两条竖向圆头纹理。选中背景和底部胶囊独立于圆，按下不缩放圆。
- 清空/自动/齿轮复用现有SVG资源。分割线、按钮和外壳走现有主题/PointLight函数，使用同一帧光源。拆分按钮共有外轮廓，主体与32.5逻辑单位扩展区独立命中。
- 选择B、切自动保持属性栏；灵敏度选择同时保留子窗。菜单外的属性点击先关菜单再执行一次。齿轮禁用并提示暂未开放，空清空禁用。Escape先关子窗，再关主栏；键盘只在Bar焦点或指针仍在Bar窗口时路由。
- 布局、dirty、窗口容量、光源区域与成功呈现后的命中快照使用同一几何。动画收敛后停止续帧。

## 全局配置、同步与持久化
新增`Drawing.Eraser.BaseDiameterDip`与`Drawing.Eraser.Sensitivity`，默认32/中；非法值分别回到32/中。
B保存16/32/64，灵敏度保存稳定枚举0/1/2。不存在影子global bool。

全Speed为开，全Fixed为关，其余为横杠混合态并提示各设备设置不同；混合点击统一开。全局开关在原eraserPreferencesMutex下改五项kind，之后只发布一次完整ProductState；保留penResponse、真实输入身份、触发门与Touch面积辅助。只改B/灵敏度、打开或关闭面板不会重写kind。

设置页五行及高度每帧读取同一快照；设置修改唤醒Bar。属性操作复用Setting既有异步配置FIFO，不打开设置页。真实Config写入/读取夹具验证64/高、五入口混合值、两种penResponse和面积开关共同恢复；GUI设置页与实际应用重启未手动操作。

普通入口一直使用ConfiguredEraser → ByEntry；本轮未发现仍在产品UI/快捷键里写强制Fixed的路径。底层FixedEraser/SpeedEraser显式API旁路仍保留，属性操作发布普通ConfiguredEraser。

## 尺寸与灵敏度实际参数
| 基础B | minimum / standard / maximum | Touch新起点 | Fixed |
|---|---|---|---|
|16 DIP|8 / 16 / 80 DIP|8 DIP|16 DIP|
|32 DIP|16 / 32 / 160 DIP|16 DIP|32 DIP|
|64 DIP|32 / 64 / 320 DIP|32 DIP|64 DIP|

SweepGain低/中/高为0.85/1.0/1.15。仅对高于fineToStandardSpeed的动作余量加增益，用于清扫资格/目标；不改尺寸上限、时间门、精细阈值、物理/DIP换算、去噪距离或Touch面积测量。中档直接返回原速度，32/中与参考解析器逐字段相等。

三种响应以各自清扫区中点速度运行2秒，实际直径DIP：
| 响应 | 低 | 中 | 高 |
|---|---:|---:|---:|
|鼠标间接DIP|48.1776|71.5544|106.274|
|屏幕笔DIP回退|51.4611|71.5545|99.4935|
|Touch DIP|53.7765|71.5543|95.2092|

这些是各模型自己的测试速度，不是同一物理速度的设备横向比较。
既有1728组精细量化回归：worstPeakToPeakDIP=0、heldExits=0；采样/帧率最大偏差1.99186%，与原中档基准相同。面积报告与接受floor转换函数未改变；floor仍受当前maximum限制。

## 清空与会话
- 唯一业务入口PublishProductCommand(Clear)，只操作当前页批注，保留Canvas视口、底图所在UI/背景路径及其他页面。Accepted后收起面板，工具与属性不变。
- 保留active.empty()执行边界：活动接触期间命令可入队，直到Up/提交后执行，不强制终止输入或清GPU假装完成。重复迟到Up不能复活内容。
- 验收发现旧Clear回撤恢复后没有Redo，白板没有内存fallback；本轮补齐每页clearRedoAvailable及白板同页boundaryFallback。Redo走原Clear事务，保存/回撤使用同一入口。新笔迹提交取消旧Clear重做；其他页面和场景保留自己的状态。
- 隐藏测试验证Desktop、Presentation、Whiteboard的Clear/Undo/Redo、当前页隔离、新笔迹取消Redo、PPT场景标记命令顺序及Up边界。
- 五入口三档固定Down、光标与首点半径一致；活动16 DIP接触在发布64后仍为16。原有非Touch Hover/Down、Up、0/20/50/100/200/500ms与长idle场景继续通过。活动接触锁存与非Touch会话算法未重构。

## 实际执行的构建与测试
ARM64原生MSBuild由vswhere查得：
`C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/arm64/MSBuild.exe`

构建命令：
```powershell
MSBuild.exe InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal
```
实际在`Build/eraser-build.ps1`中通过vswhere动态定位，保持完整solution及PptCOM依赖。最终退出0，6条warning均来自已有hashlib++头。四个Draw3 shader已编译并嵌入；本轮仅提取同值视觉宏，CPU/GPU数据布局不变。

首次MSBuild因沙箱系统TEMP目录拒写而未进入编译；随后仅在当前PowerShell进程保存并规范化Path，设置MSBUILDDISABLENODEREUSE=1，TEMP/TMP指向Build/eraser-attribute-temp，原PATH内容保留。未修改全局环境、SDK或工具链。

| 测试命令 | 结果 | 实际耗时 |
|---|---|---|
| `InkeysHeadlessTests.exe --no-window` | 0，通过 | 36.55s |
| `Inkeys.exe --bar-eraser-offscreen-test` | 0，通过 | 1.19s |
| `Inkeys.exe --draw3-hidden-test` | 0，通过 | 2.25s |
| `Inkeys.exe --draw3-eraser-hidden-test` | 0，通过 | 111.27s |

每个隐藏测试允许600秒。测试过程按主Agent顺序运行，没有可见主窗口或Computer Use。通用隐藏测试使用项目已有的屏幕外测试HWND；离屏验收不创建HWND。

180组DPI(96/144/192)×UI缩放(0.5/0.75/1/1.5/2)×方向/位置组合全部通过，每组还附带240 DIP超高圆、300逻辑单位窄工作区及菜单方向锁定检查。离屏测试调用生产面板与标准按钮，并检查真实clickFunc、Pointer、Keyboard及idle状态。

### 测试纠正的依据
原通用隐藏用例错误要求“有内容→有内容”翻页必须增加contentRevision。Host的ObserveCurrentPageContent只在空/非空变化时发布，这段产品代码未修改。断言改为命令/页号/内容/呈现与输入回收证据；日志实际记录page=1、content=1、revision=5/previous=5且呈现帧增加。
新增活动Clear用例最初错误地等接触未Up就清空，已改为验证原有合法Up边界。新增白板用例使用独立PublishProductWorkspace入口，不能通过工具属性发布隐式切场景。
最终无失败断言或输入模型错误。通用夹具未启动真实自动保存worker，save_submit/load_submit日志为该夹具的已知限制；真实异步磁盘完成/Office重启路径不据此宣称验收。

## 截图与视觉验收范围
截图由生产BarEraserAttributePanel、DrawBarButtonVisual和共享UI设备离屏绘制，主栏标准按钮用于参照，并非运行中桌面截图。
- `Build/eraser-attribute-visuals/0-dark.png`：96 DPI，100% UI。
- `Build/eraser-attribute-visuals/1-light.png`：对应浅色。
- `Build/eraser-attribute-visuals/2-dark.png`：144 DPI混合态。
- `Build/eraser-attribute-visuals/3-light.png`：144 DPI向下/反向布局及边缘定位。
- `Build/eraser-attribute-visuals/4-dark.png`、`5-light.png`：192 DPI、65% UI，真实64 DIP圆高于面板仍不缩小。
已查看离屏图片，确认图标、圆形预览、分组、胶囊、拼接按钮、深浅主题和上下裁剪。初次离屏发现SVG目标尺寸没有进入val，已修复并重绘。

**未完成实机GUI视觉/交互验收**：真实鼠标/笔/Touch手感、实际多显示器/DPI切换、停靠直拖中的每一帧、真实底图图片、详细设置窗口与重启、Office/WPS与Windows7未手动验证。已有自动测试不代表所有设备/系统验收。DPI/device-generation失效路径做静态审查，未注入真实设备丢失。

## 日志与文件
- 最终构建：`Build/eraser-attribute-build-delivery.log`。
- shader成功链：`Build/eraser-attribute-build-2.log`。
- 完整机器可读结果：`Build/eraser-final-results.json`。
- 测试日志：`Build/eraser-final-{headless,offscreen,commands,eraser}-{out,err}.log`。
- 离屏细节：`Build/eraser-attribute-offscreen-results.log`。
- 原始确认规格：[request.md](request.md)；实施设计：[design.md](design.md)；格式检查：[format-check.md](format-check.md)。

业务/测试修改文件（不包含原有PptCOM.dll改动）：
- `Inkeys/IdtMain.cpp`
- `Inkeys/IdtState.cpp`
- `Inkeys/IdtState.h`
- `Inkeys/Inkeys.vcxproj`
- `Inkeys/Inkeys/Drawing/Draw3/Assets/inkPixelShader.hlsl`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.DrawingController.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.SpeedEraser.h`
- `Inkeys/Inkeys/Other/Other.Config.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.Button.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Interaction.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.Main.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.RenderLoop.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.State.cppm`
- `Inkeys/Inkeys/UI/Bar/Bar.ToggleClickCoalescer.cppm`
- `Inkeys/Inkeys/UI/Setting/Setting.cpp`
- `Inkeys/Inkeys/UI/Setting/Setting.cppm`
- `InkeysHeadlessTests/InkeysHeadlessTests.vcxproj`
- `InkeysHeadlessTests/animation_tests.cpp`
- `InkeysHeadlessTests/speed_eraser_tests.cpp`
- `Inkeys/Inkeys/Drawing/Draw3/Assets/EraserGripVisual.h`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.Test.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttribute.cpp`
- `Inkeys/Inkeys/UI/Bar/Bar.EraserAttributeLayout.cppm`
- `InkeysHeadlessTests/eraser_attribute_tests.cpp`

另新增本任务PRD/设计/实施/验证记录及native-desktop/eraser-attributes.md合同，并在native-desktop索引登记。

## 2026-09-16 自动总开关与布局追加验证

本节覆盖本轮追加需求，并替代上文关于433.5 DIP面板、32.5 DIP扩展区、Mixed横杠和“无独立总开关”的历史描述。

- 新增`Drawing.Eraser.Automatic`，缺失默认开启；关闭只门控最终解析，不改写五入口配置，显式Speed同样解析为Fixed，重新开启后恢复入口配置。
- 面板默认宽342 DIP；圆组四段16 DIP，外部与中央四段标准留白5 DIP；窄区先压缩5 DIP、再压缩16 DIP，圆和按钮尺寸不缩小。
- 自动按钮为90×70 DIP，命中区70/20 DIP；内部1 DIP分割线上下留5 DIP，与整体共享选中青色光影及按压缩放。
- 橡皮面板、菜单和提示在主栏绘制前提交，主栏最终覆盖全部橡皮浮层。
- 设置页新增自动粗细总开关卡片；关闭期间五入口仍可编辑和持久化。

验证结果：完整`InkeysRepo.sln`的`Debug | ARM64`构建退出0；Headless、橡皮离屏、Draw3隐藏、橡皮隐藏四项均通过。Headless报告216组布局、0失败。通用Draw3隐藏测试首次在异步resize/present等待处超时但进程退出0，立即复跑全部隐藏集通过；该资源缩放路径未被本轮改动。`git diff --check`通过，仅报告既有autocrlf提示。未启动交互式GUI，未提交、推送或归档。

## 2026-09-16 精细视觉与交互追加验证

- 已删除橡皮属性专用悬停内容提示矩形及其Bounds、命中和续帧状态。
- 24/32/40 DIP选中态为橡皮外3 DIP间隙、1 DIP Accent圆环，PointLight及透明度可逆渐变；按压复用普通按钮的0.95缩放与Press/Release曲线。
- 点击区域为当前可见裁剪内、圆外扩5 DIP的真实圆形；外接矩形角和两个尺寸圆之间的跨区释放均不执行。
- 自动内分割线与主栏分割线等高居中；右侧箭头复用`barThicknessAdjust` SVG及笔类型0/180度动画，保留70/20动作区和整体按压。

完整`InkeysRepo.sln` `Debug | ARM64`构建通过；Headless通过并报告216组布局、0失败；`--bar-eraser-offscreen-test`通过且`failures=0`。离屏测试保存并断言尺寸圆按下/释放、选中环交接和箭头开合中间帧，最终均能回到idle。`git diff --check`通过。未启动交互式GUI，未提交、推送、归档或结束任务。

## 2026-09-16 主栏直拖换边追加验证

- 直拖锁存测试确认：跨越临时工作区边界时，橡皮面板保持已呈现的位置、上下与左右方向；松手后开始既有收拢→换边→展开动画，并最终稳定在新侧。
- 完整`InkeysRepo.sln` `Debug | ARM64`构建退出0；`InkeysHeadlessTests.exe --no-window`通过（EraserAttribute layouts=216，failures=0）；`Inkeys.exe --bar-eraser-offscreen-test`退出0。
- 生产离屏测试新输出`Build/eraser-b/visuals/narrow-eraser-attribute.png`：300×620工作区、面板打开且菜单关闭，确认窄区只裁剪溢出、不缩小圆或自动粗细按钮。
- 未启动交互式GUI；`Inkeys/PptCOM.dll`为既有未提交变更，未纳入本轮提交。任务继续保持in_progress。

## 2026-09-16 窄区正常布局追加验证

- 本节替代上文“窄区先减留白/压缩5 DIP与16 DIP”的历史记录：任何工作区均采用342 DIP自然布局、四段16 DIP圆组留白和四段5 DIP中央/自动留白；空间不足时只裁剪横向溢出。
- 纯布局与生产离屏断言会同时验证圆、自动按钮不缩放，以及5/16 DIP留白不进入紧凑模式。
- 完整`InkeysRepo.sln` `Debug | ARM64`构建通过；`InkeysHeadlessTests.exe --no-window`报告EraserAttribute layouts=216、failures=0，`Inkeys.exe --bar-eraser-offscreen-test`通过并更新300×620 PNG；`Inkeys/PptCOM.dll`继续排除在提交之外，任务保持in_progress。
