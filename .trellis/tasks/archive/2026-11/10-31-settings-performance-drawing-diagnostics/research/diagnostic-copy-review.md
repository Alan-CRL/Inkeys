# R10 诊断全称与复制复核

日期：2026-11-01。角色：trellis-check。只读检查本轮R10及必要本机SDK头，写本记录；未修改源码、未构建/重跑测试、未启动GUI、未操作真实剪贴板、未提交。

## 结论

当前未发现遗留源码阻塞项。两个诊断块均在块外下方以标准按钮右对齐复制同帧完整字符串；SDK名称、flags、SampleDesc、HRESULT和设备/输入全称已接入原owner摘要。组合或未映射值保留真实数值，没有把未知数据改成启动模板。原查询线程/时机、初始化顺序和回退逻辑未因R10改变。

首轮构建发现的 `DXGI_ALPHA_MODE_FORCE_DWORD` 聚合初始化C2397窄化已在单项常量处显式转UINT修正，详情见末节。最终ARM64重建和专项退出码由根代理集中记录，本复核不提前宣称运行通过。

## 1. 两个复制入口与文本一致性

- `Setting.cpp:4315`—`:4321`：本帧只调用一次Host::CanvasInformation取得局部std::string，先RenderEdidCard显示，再CopyButton传同一局部字符串。点击没有再读一次Host，也没有跨线程查询设备。
- canvas卡片和复制按钮位于同一明确PushID作用域内；显示器复制使用另一个稳定id，helper自身也PushID，因此两个同名“复制”按钮不会共享身份。
- `:4324`—`:4376`：EDID文本从本帧同一DisplaySnapshot构造，循环遍历全部monitors，完成后先显示完整edidText，再复制同一edidText。快照不可用时原不可用状态仍在字符串中，不会复制空模板或只复制可见屏。
- CopyButton（`Setting.Controls.cpp:690`）使用现有Button/ControlFontScope、ButtonWidth自然宽度和ControlHeight=32 DIP；宽度仅在父内容不足时受available限制。按钮在RenderEdidCard结束后调用，位于父内容作用域，而不是信息卡内。
- x按`origin.x+available-width`靠右；按钮正常推进纵向游标后只恢复原x，后续标题仍左对齐，不会回绕到信息块内部。
- 仅Button返回pressed时ImGui::SetClipboardText(text)；直接传完整UTF-8字符串，不读取已换行/剪裁后的顶点或屏幕可见片段。没有新增剪贴板线程/权限流程。
- 字符串生命周期安全：两个局部std::string存活覆盖同步显示和按钮点击处理；helper不保存char*到异步状态或下一帧。

## 2. SDK名称与真实值

Diagnostics模块新增的FormatDiagnostic*接口均在cppm中声明、导出，包含所需D3D/DXGI/std::string类型；TransparentPresentation、RealtimeStylus、GraphicsInitialization沿既有diagnostics import可见，不从UI调用GPU对象。

| 用户要求 | 当前实现与复核 |
| --- | --- |
| Feature Level完整常量 | FormatDiagnosticFeatureLevel输出例如D3D_FEATURE_LEVEL_11_1及0x0000B100，11_0/11_1实际设备值不再被当作API版本小数 |
| 格式/SwapEffect/Alpha/Scaling | 已收录常量使用完整DXGI_*前缀并附原数值；实际BGRA8/FLIP_SEQUENTIAL/PREMULTIPLIED/STRETCH均有精确映射 |
| 未映射enum | 统一“未知 类型 (真实值)”，不假称某个已知常量，也不变为零；不声称映射穷尽SDK全部DXGI_FORMAT成员 |
| Flags | 与本机Windows SDK 10.0.26100.0 shared/dxgi.h枚举逐项核对，13个当前DXGI_SWAP_CHAIN_FLAG_*全部在表中，包括FRAME_LATENCY_WAITABLE_OBJECT |
| 组合/未知位 | 逐已知bit追加完整名称并用`|`组合，从unknown中清除已知位；剩余unknown单列8位hex，末尾总flags仍完整8位hex |
| flags=0 | 显示“无标志 (0x00000000)”，没有编造SDK不存在的NONE枚举 |
| SampleDesc | Count与Quality分别标出，仅Count=1且Quality=0注明无MSAA；其它输入值原样保留，不误把Quality当样本数 |
| HRESULT | 常见COM/Win32通用/DXGI结果显示SDK符号和8位十六进制；未知结果明确标未知，并以UINT保留32位原始位 |

`DiagnosticHex`使用固定11字节缓冲容纳0x+8位+终止符。flags表中的值在本机SDK均为互斥单bit，因此`flags & value`及清位计算不会因复合掩码误展开。哨兵、未知位和负HRESULT转UINT保留的是原32位模式，未丢符号位。

## 3. owner摘要、全称和原始状态

- `TransparentPresentation.cpp:789`对实际graphics->featureLevel调用纯formatter；`:834`对真实GetDesc1结果调用FormatDiagnosticSwapChain。元数据仍在原初始化/恢复/Resize边界读取，UI没有新增COM/驱动枚举。
- 硬件实际driverType显示Hardware（硬件GPU），WARP软件与硬件失败回退说明保持。
- 主ULW和辅助前置表面均使用UpdateLayeredWindow (ULW)全称；DirectComposition主模式和Composition/HWND交换链类型保留，未将辅助ULW冒充主呈现方式。
- UMD标签扩展为“用户态图形驱动（UMD）版本”，原四段版本值/失败状态保留。
- Host输入后端使用RealTimeStylus (RTS，扩展属性/基础X/Y)，Win32鼠标兼容回退及不可用仍可区分。
- RealtimeStylus只将已取得的profile/stage/HRESULT送入FormatDiagnosticRtsAttempt。ProfileName实际返回Extended/BasicXY，与映射精确一致；中文就绪/失败与原SUCCEEDED判据一致，失败stage不丢。
- 原控制台DiagnosticOutput::Printf的英文格式、字段和调用位置保持，不因设置摘要中文化而改变采集脚本或输入选择。
- adapter/GetDesc/CheckInterfaceSupport/GetDesc1失败及lastFailure都保留错误信息；没有为了显示完整名称更换创建参数、waitable回退、RTS BasicXY重试或鼠标回退条件。
- 全程使用std::string UTF-8；原adapter宽字符串仍CP_UTF8转换，EDID逐屏宽字符串仍沿既有utf16ToUtf8，不新增ANSI转换或字符截断。

## 4. EDID与DPI

- 信息块起首仅一次 `EDID (Extended Display Identification Data)`，逐屏仍保留EDID解析状态与原始物理尺寸，避免每屏重复全称。
- DPI明确为`DPI (X/Y): effectiveDpiX × effectiveDpiY`，取值两轴分别读取，使用乘号，不把Y误复制为X。
- 原活动拓扑、监视器数量、主屏、Windows显示名、分辨率、可信物理尺寸/失效原因均继续追加；全屏循环没有被改成只处理primary或当前viewport。

## 5. 测试是否覆盖真实行为

### CopyButton专项

`CheckDiagnosticCopyButtons`（`setting_design_tests.cpp:1963`）实际调用RenderEdidCard和新CopyButton，通过真实ImGui鼠标Down/Up触发Button，不是直接调用SetClipboardText假装点击：

- 多DPI、明暗主题、260/640 DIP宽度下，断言实际item矩形的标准自然宽度/32 DIP高度、右边缘对齐和位于卡片下方；后续真实标题必须回到左边缘且在按钮之后。
- 两个同label按钮置于相同父作用域，id必须不同，证明helper自身PushID有效，而不只靠外部PushID碰巧区分。
- 长中文多行与四屏文本先滚动到卡片头部离开viewport但按钮可见的位置，再点击复制；fake clipboard捕获内容必须与整段源字符串逐字相同。
- 点击后在同一测试更新canvas文本，再复制并断言新帧完整内容，能发现错误缓存或二次读取造成的不一致。
- ClipboardProbe拦截并恢复ImGui Platform_SetClipboardTextFn，保存完整UTF-8值，不调用真实系统剪贴板。
- attention-only挂载了该专项；copy-only在zh-CN/zh-TW/en-US真实标签循环中经CheckCopyAndCardPolish调用，因此三语言复制按钮标签的覆盖实际可达。

### 诊断纯值probe

`RunDiagnosticFormattingProductionTest`经既有`RunGraphicsDeviceSelectionTest`及`--draw3-device-policy-test`早退链调用。新probe只格式化枚举、数值和DXGI_SWAP_CHAIN_DESC1值结构，不创建GPU、交换链或HWND。

断言覆盖实际常量、未知enum、零/组合/未知flags及总hex、SampleDesc合法/不同Quality、S_OK/常见/未知HRESULT、RTS就绪/失败及组合desc。它检查完整预期字符串和组合摘要保留字段，能发现短名称/丢未知位/误用默认值，而非只断言函数返回非空。既有设备选择测试仍用注入的创建设备结果，不调用真实D3D11CreateDevice。

## 6. 首轮源码问题与格式状态

- 根代理报告首轮ARM64编译在Diagnostics.cpp:315出现C2397：DXGI_ALPHA_MODE_FORCE_DWORD被隐式放入UINT聚合成员，MSVC判有符号窄化。
- 已只对该表项使用`static_cast<UINT>(DXGI_ALPHA_MODE_FORCE_DWORD)`。本机SDK shared/dxgi1_2.h:548定义其值为0xffffffff；显式转换保留完整32位哨兵，与DiagnosticEnum输入类型一致，不修改错误值或已知名称。
- 本代理已静态复核此修复，未代为重跑编译；最终相同Solution/Debug|ARM64重建退出码待根代理记录。
- 当前所有修改源文件BOM相对HEAD保持、工作树CRLF；`git diff --check --ignore-submodules=all`通过。
- PRD/design R10与code-spec的诊断文本/复制七节合同均和当前owner、按钮、纯formatter及focused测试边界一致。未增加业务动作、初始化流程、查询线程、工程配置或网页依赖。
