# 手掌橡皮

## 1. Scope / Trigger

修改Draw3按接触面积自动转橡皮、Drawing.PalmEraser配置、详情页、邻域抑制或断触接续时适用。产品路径是Inkeys/Inkeys/Drawing/Draw3；不修改旧Draw2/demo绘制镜像。这里记录代码合同及验证入口，不等同真实RTS硬件或GUI验收。

## 2. Signatures

- `PalmEraser::Settings { enabled, thresholdPercent, recognitionWindowMs, parallelWriting, tools[7] }`，`ToolCategory::{Pen,HardPen,Highlighter,Laser,Eraser,Shape,Brush}`位于Draw3.PalmEraser.h。
- `ResolveScale(DisplayScale,InputSource)`、`ResolvePreviewScale(DisplayScale)`、`ThresholdAreaPixels(Settings,Scale)`；Scale区分可信mm/px与估算系统DIP/px，不伪造EDID密度。
- `PalmEraser::State::{Begin,Observe,Advance,End}`：可在副本上观察，转换准备成功后由owner提交。
- `Host::{SetPalmEraserOptions,PalmEraserOptions}`发布/复制期望值；PumpBridgeState在绘制owner应用Controller，不能借设置渲染线程跨线程操作Controller。
- `PalmEraserPreferencesSnapshot()`、`SetPalmEraserPreference(Settings)->bool`为产品设置入口；setter自行调用既有RequestConfigWrite冻结保存，页面不能再排一次。
- `PalmEraserDraft::{referenceAreaPixels,referenceAreaCm2}`都是100%参考面积；前者来自实际像素阈值，后者仅在可信物理标尺下由px²×mm/px×mm/px÷100得到，不写入配置。
- `Design::RenderPalmEraser(PalmEraserDraft&)->PalmEraserEvents`；`RenderDrawingEraserEntries`提供现有笔速入口及其下方手掌入口。
- `RunPalmEraserProductionProbe()`通过早期`--draw3-palm-eraser-test`运行生产共享CPU helper，不创建HWND。

## 3. Contracts

### 配置、标尺与定尺

- main.json的`Drawing.PalmEraser`为NoUpload：Enabled冻结大屏默认开/其他关；ThresholdPercent整数1..400、默认100；RecognitionWindowMs只允许100/150/250/500、默认150；ParallelWriting默认true；Tools七项bool默认全true。先校验JSON原类型和整数宽度，不能用窄化后的值识别合法字段。
- 缺省大屏分类复用ConfigDetail::GetDefaultLargeScreenDevice，与用户保存的PaintDevice、换屏或EDID当前可用位分开。EDID失效不关闭已启用功能，也不覆盖保存的开关。
- 可信显示物理映射/有效现有标定用25cm²=2500mm²；不可用时改用100×100系统DIP=10000DIP²。两者都按面积乘ThresholdPercent/100。估算路径用`dipPerPixelX/Y`换画布像素，100DIP在100%/150%系统缩放分别对应100/150px，不把系统DIP再乘设置UI缩放。
- 实际InputDeviceType必须是Touch，明确TouchPad/Mouse/Pen不能识别；旧来源Unknown只允许保守DIP回退。面积必须已有CanvasPixels元数据、宽高有限且为正、max/min≤3.5，不能靠压力/DPI猜未知面积单位。
- State锁存独立Down偏好与标尺，观察既有合并快照中的有效最大面积；不承诺硬件全部包峰值。识别指标保持areaPx=W×H，达到原阈值即激活；2026-11-06按用户选择A，定尺改为椭圆近似等面积圆，直径sqrt(W×H)、半径为直径的一半。识别、阈值、配置和设置矩形预览均不额外乘π/4。截止QPC样本可接收，之后Advance锁尺；第一次物理Up提前封窗冻结。
- 窗口内尺寸更新只影响新增擦除几何，旧realPoints半径不回写；窗口后及成功接续不重新采样或增长。不使用SpeedEraser面积辅助的2..96DIP上限/拖动资格或速度尺寸控制器。

### 转换、并行和续接

- Palm仅覆盖本Runtime实际tool为Eraser/Fixed，保留selectedTool/global选择/其他接触。原绘制转手掌先准备新模型，再移除本触点CPU几何/Shape/自有Laser层并重建共享L0/L1；不清L2或文档。原实际工具已是橡皮时保留已接受擦除及旧半径，只改后续尺寸，不能恢复已擦内容。
- 原地面积变化必须被消费，不能被“位置未变”早退吞掉。截止不依赖继续Move；休眠复用既有deadline及Move订阅/sequence/generation握手。
- 邻域半径为r+max(0.15r,4px)。近邻新独立触点锁存禁止手掌，在其他准入允许时仍可绘制；同批先激活者/Down顺序确定性仲裁。成功续接前的有限候选保留邻域预约；已激活者后来靠近不反向取消。
- 全局多指关闭拒收额外新独立Touch；真正断触接续优先，不当作额外独立输入。激光仍受其原多指子偏好约束。
- ParallelWriting=true保留其他已准入绘制；false正常收尾其他绘制并保留墨迹，物理手掌期间不接收新绘制，旧route隔离到真实终态。新Touch可仅观察面积成为远处手掌，仅观察状态不得产生modeler几何/光标/Stored，超时或终态取消回收。
- 强制普通Up也必须完成对应工具的副作用。Laser复用FinalizeLaserRuntime，结束生命周期、将接受轨迹冻结为自有completedPoints、合并dirty；不得保留指向回收Runtime的layer或把activeContactCount漏在非零。
- 成功断触接续沿用旧工具、同一手掌尺寸与逻辑笔划，不Begin新识别窗。原绘制遇大面积接续仍绘制。旧手掌候选按Eraser/Fixed实际身份匹配，同时保留原selectedTool和既有时间/位置/方向门槛；普通Laser/Shape不扩展续接，已经转手掌者可按实际Touch Eraser续接。只有模型Update成功才交接handle，失败/超时按新独立输入处理。
- Touch Palm Fixed长时间无模型输入后，复用有界model-only重锚；空档用最后成功建模时间判断，不能使用已被本次尝试提前覆盖的lastModelInputTime。保留旧几何、尺寸、身份，不增加输出上限、不改普通Fixed行为。
- 沿用既有整笔抬笔/完成提交Z序、Undo/Redo及UInk，不实现真实时序互盖、时间日志或私有分组。偶然擦写交叠不保证与实际输入时间相同的层级。

### 设置详情

- 新绘制子页PalmEraser紧跟笔速橡皮入口，复用子页PushID/导航/返回/滚动；不增加根侧栏或改变笔速齿轮的打开目标。
- 页标题→条件Warning→基本设置（首项总开关）→窗口下拉→面积倍率及同条目重置→像素预览→七工具勾选→精细控制；无介绍块。150ms显示推荐后缀；reset只改倍率，关总开关仍可编辑偏好。
- 边擦边写行的标准i仅在ParallelWriting=true且全局多指或激光多指关闭时显示。读取Drawing.MultiTouchEnabled及Drawing.Laser.MultiTouchEnabled的当帧只读draft，不改变偏好；全局关闭优先提示全部无法生效，否则只提示激光笔下无法生效。两个多指均开或边擦边写关时无提示，不额外受手掌总开关影响。
- Brush现在可勾选且保存，默认true；无Brush工具选择功能不意味着把偏好灰显。Shape统一映射已有几何工具，工具类别不按DrawingTool枚举位置推断。
- 预览与识别同用已应用绘图输出快照；根据同一阈值显示单个主题accent色圆角矩形描边（不填充），默认1:1；左上角悬浮Combo可选1:1、4:3、2:1、1:2。比例是页面状态，不写入偏好；Combo悬浮于边框上方，不挤占矩形布局或换行；按用户确认保留原控件透明度，允许边框透出，不另加不透明底色。图形直接用像素，文字留白正常UI缩放；边框未撑满时独立水平居中，下拉仍在预览左上角，超宽可横向查看；不能缩小伪装实尺。阈值说明仅保留调倍率的用户说明；下方复用标准SettingRow note显示i及当前比例/倍率的cm×cm和px×px。可信物理面积由同一参考像素面积乘mm/px的面积换算给draft；估算仅px，未发布不虚构尺寸。
- Warning以已应用Palm预览scale.available/estimated和已发布monitor判定，EDID失败明确仍可使用；未发布不伪报故障。预览横向-only子视图局部使用ImGui::BeginChild及既有DIP scrollbar样式，避免通用纵向补间将横轨压为零；不改全局Controls。

## 4. Validation & Error Matrix

| 场景 | 结果 |
| --- | --- |
| 面积/比例合格，窗口未结束 | 当次即转本触点橡皮，旧原工具临时内容清除 |
| 合格已激活面积100×100px / 160×90px | 圆直径分别100px / 120px；光标和新增几何一致 |
| 面积单位缺失、非法、过扁或窗口外首次达标 | 保持原工具，不猜单位或迟触发 |
| 原地摊掌、无后续Move到截止 | 可激活并准时锁尺，不依赖位移 |
| 原橡皮转Palm、临时尺寸变大 | 旧擦除及半径保留，仅后续点使用新尺寸 |
| 物理标尺不可用 | 功能仍可用，10000系统DIP²回退，Warning显示 |
| 缺省大屏且EDID失败 | 默认Enabled仍true，不被物理可用位覆盖 |
| 近邻新Touch/多指关闭 | 近邻只禁手掌；单指模式拒收额外独立Touch |
| 并行关闭中断Laser | 轨迹冻结且生命周期归零，不借用回收对象 |
| 普通绘制成功续接遇大面积Down | 原绘制继续，不重开手掌识别 |
| 手掌成功续接遇新面积/新设置 | 保持同一手掌尺寸，最终仅一history动作 |
| 模型接续或转换准备失败 | 不提交半转换，不丢旧CPU内容或候选 |
| 超输出窗口长停顿后Move/Up | 有界重锚后继续擦，旧点/尺寸/身份不变 |
| 设置UI缩放/系统DPI改变 | UI倍率不缩放预览几何；系统DPI正确换算像素 |

## 5. Good / Base / Bad Cases

- Good：A在左侧继续写，B在右侧用手掌擦；B的邻近碎触点不能再变橡皮，远处C可独立擦除。B短暂断触接续仍保持旧大小。
- Base：Enabled=false或工具未勾选时沿普通绘制/橡皮与保存路径；真实时间Z序不进入本功能。
- Bad：EDID失败直接禁用、用189DIP边长冒充确认的100DIP回退、先分类新Down再续接、重做时引入新的时间Z序、清全画布掩盖临时几何残留。

## 6. Tests Required

- `InkeysHeadlessTests.exe --no-window --setting-eraser-only`：真实ImGui入口/控件、三语/主题/窄宽/DPI、Brush可编辑、窗口推荐、只重置倍率、预览顶点等面积与像素独立性、未满宽居中/超宽横滚、标准i尺寸提示的可信/估算/不可用分支与四种比例、1%下限、策略/现有橡皮回归。
- 页面顺序断言用真实控件矩形/Warning背景DrawData，并结合标题与分组源码顺序；Design::TextAt直接AddText不进入ImGui日志，不能把日志缺项当作界面缺项。
- `InkeysHeadlessTests.exe --no-window`全范围回归；纯策略单独可用`--palm-eraser-only`，覆盖2500mm²/10000DIP²、倍率、系统缩放、无面积/比例/时间边界、状态锁存及pool复用；另断言100×100→100、160×90→120、交换宽高/同比缩放、阈值等于/略低/略高，防止把识别面积也改成椭圆面积。
- 产品早期`--draw3-palm-eraser-test`：生产CPU转换/失败回滚、各原工具、擦除旧点保留、观察only/邻域/单指、真实续接身份和模型失败、Laser强制收尾、四档model profile的长空档Move/Up。该probe不是完整Run或GPU像素/硬件证据。
- `--config-boundary-test`：真实Config ReadAll/ReadMini/写盘/赋值/Reset/NoUpload、非法类型/宽整数、冻结大屏与EDID失效下DIP识别；不增加Config复制构造绕过shared_mutex。
- 三语i18n sync/check、完整InkeysRepo.sln Debug|ARM64、原生ARM64 MSBuild及编码/EOL/minimal diff检查；GUI/实机未测应明确说明。

## 7. Wrong vs Correct

```text
Wrong: 新Down → 重开150ms并按新面积换工具 → 尝试把它接到旧笔。
Correct: 既有匹配+模型Update成功 → 继承旧工具/手掌尺寸；未匹配才Begin新独立触点。

Wrong: EDID失效 → 关闭Enabled，或者把25cm²按普通桌面毫米/DIP换算成回退。
Correct: 保留Enabled → 10000系统DIP²×面积倍率换像素 → Warning说明估算。
```
