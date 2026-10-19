# Research: 旧版 PowerPoint / WPP 兼容性与 COM 损坏边界

- Query: 核查 dynamic 优先修复是否保住 PowerPoint 2010（MSO14）、WPP/WPS Presentation 2013，以及产品既有 PowerPoint 2007 声明；区分损坏范围、可选功能、真实提供方和静态证据。
- Scope: mixed（仓库源码/规范/历史研究、只读注册表和文件元数据、微软与 WPS 第一方资料）。
- Date: 2026-10-18（Asia/Shanghai）。
- Phase: planning；仅本研究文件落盘。未修改产品代码、规范、项目配置、DLL/TLB，未构建、运行 COM 夹具、激活 Office、启动 GUI 或执行 git 操作。

## Findings

### 结论与证据等级

1. **静态确认**：当前产品文案仍声明 PowerPoint 2007 / WPS 2013 及以上。不能未经授权将最低 PowerPoint 版本提高到 2010 或 2013。
2. **官方文档确认成员早已存在**：SlideID 在 Office12（2007）互操作文档已存在；View.State、PointerType、View.Slide、Next/Previous/Exit 在 Office2003 文档已有。新增页身份与生命周期读取没有因为这些成员本身而天然要求 Office15。成员存在不等于本产品通过旧版实机验收。
3. **静态确认兼容风险**：生产新 descriptor 使用反射 accessor；HWND 使用 PIA cast 特例；新会话原生准入需要有效 HWND。PIA/QI 异常但 dynamic 页码可用时，核心链路仍可能因缺 HWND 失效。普通属性应优先 dynamic；HWND 不能靠一律替换为 dynamic 就宣称兼容。
4. **第一手静态新证据**：仓库随附 PowerPoint PIA15 的 `SlideShowWindow.HWND` getter 带 `TypeLibFunc(FRestricted)`。微软 C# COM binder 源码扫描会跳过受限成员；历史真实 PowerPoint 探针也已证明反射 HWND 返回 `0x80020003`、类型 HWND 成功。应将该标量列入有证据的有限例外，类型接口失败后必须能走经过身份验证的 Win32 回退。
5. **兼容边界**：dynamic 能减少对具体 PIA 类型接口的依赖，不能修复所有类型库、ITypeInfo、IDispatch 或 COM 激活故障；以目标 .NET Framework 运行时中每项实际调用的能力为判据。严重类型信息异常本身也可能使 DLR 失败。
6. **本轮未验证**：PowerPoint 2007/2010、WPP2013 均无当前实机验收。本机只发现 Office16 注册/文件；不能用该版本或 fake ledger 替代旧版支持结论。

### Files found

| 文件 | 职责 / 证据 |
| --- | --- |
| `PptCOM/PptCOM.cs` | 生产 dynamic 根对象、ROT/提供方发现、事件订阅、轮询、窗口获取、批注和翻页等命令。 |
| `PptCOM/PresentationDescriptor.cs` | 新文稿/页拓扑读取；统一反射 accessor、类型 HWND 例外、SlideID/State 降级与 temporary 释放。 |
| `PptCOM/SlideShowSession.cs` | 纯值会话缓存、owner mailbox、受会话保护的 Exit；Exit 复用同一 HWND accessor。 |
| `PptCOM/PptCOM.csproj` | .NET Framework 4.0 / AnyCPU；PIA15 + EmbedInteropTypes；输出 TLB。 |
| `Package/Microsoft.Office.Interop.PowerPoint.15.0.4420.1018/lib/net20/Microsoft.Office.Interop.PowerPoint.dll` | 仅加载托管元数据；读取 HWND 的 DispId/FRestricted，未创建任何 COM 对象。 |
| `Package/MicrosoftOfficeCore.15.0.0/lib/net35/Office.dll` | 上述元数据读取时所需托管依赖。 |
| `Inkeys/IdtPlug-in.cpp` | 新会话和文稿/页/HWND/PID 的准入检查。 |
| `Inkeys/src/i18n/{zh-CN,zh-TW,en-US}.jsonc:509` | PowerPoint2007 / WPS2013+ 的现有产品支持声明。 |
| `.trellis/spec/ppt-interop/index.md` | 声明/实现/验证分级；禁止无依据扩缩支持范围。 |
| `.trellis/spec/ppt-interop/com-contract.md` | 当前把常规读取固定为 InvokeMember，和用户 dynamic 优先要求冲突。 |
| `.trellis/spec/ppt-interop/native-session-ui3.md` | HWND 准入、会话身份、Unknown、退出、可选批注能力合同。 |
| `.trellis/spec/native-desktop/build-and-compatibility.md` | 构建与产物交接；静态配置不等于架构/提供方实测。 |
| `.trellis/tasks/archive/2026-09/09-25-ppt-ui3-scene-and-page-sync/research/recognition-regression-state.md` | 历史真实 PowerPoint HWND 反射失败/类型接口成功证据，以及修复和验证局限。 |

### Code patterns and compatibility effects

| 位置 | 当前行为 | 对旧版/损坏设备的影响 |
| --- | --- | --- |
| `PptCOM/PptCOM.cs:92` | application / presentation / showWindow 为 dynamic 根字段。 | 声明是 dynamic 不代表新增访问没有类型接口/反射依赖。 |
| `PptCOM/PptCOM.cs:758` | 发现 `PowerPoint.Application`、`KWPP.Application`、`Wpp.Application` / `WPP.Application`。 | 提供方按注册能力发现；不能把 WPS 年份当作 MSO 主版本。 |
| `PptCOM/PptCOM.cs:1259` | ActivePresentation、SlideShowWindow 动态获取。 | 类型接口不通时仍可能取得主 COM 图。 |
| `PptCOM/PptCOM.cs:1311` | PIA Application cast 后绑定事件，失败设 `forcePolling=true`。 | 保底方向正确；部分绑定后失败的回滚和事件可靠性须由其他审计覆盖。 |
| `PptCOM/PptCOM.cs:1483`、`:1593` | forcePolling 启用维护/页更新和 descriptor 强制刷新。 | 无事件不应直接等于无联动；性能、busy 和低频恢复不可由版本号代替测试。 |
| `PptCOM/PptCOM.cs:1643`、`:1784` | 旧 GetPptHwnd 先 PIA HWND；失败走 Win32 查找。 | 旧路径的存在不能证明新会话已经消费其兜底。 |
| `PptCOM/PresentationDescriptor.cs:82`、`:106` | HWND 先类型接口，其他属性统一 InvokeMember。 | 需改为普通成员 dynamic；HWND 为有依据的例外，不能类型接口失败就认定会话不可用。 |
| `PptCOM/PresentationDescriptor.cs:228` | HWND 普通失败吞掉并返回 0。 | 新 native 准入需有效 HWND，可能出现有效页但整场不显示。 |
| `PptCOM/PresentationDescriptor.cs:309` | State=5 判 EndScreen；缺 State 普通错误时继续同次 Slide 读取；未知整数不推断结束。 | State 的成员年龄不是新风险；WPS 是否使用相同语义须验收，缺 State 不应误报结束。 |
| `PptCOM/PresentationDescriptor.cs:325`、`:362`、`:408` | SlideID 缺失/拓扑失败显式 PageIndexFallback。 | 适合旧版/部分提供方保住页码功能；不得伪造 ID 或跨文稿持久恢复。 |
| `PptCOM/PptCOM.cs:1908`、`:1928`、`:1942` | dynamic View/Slide/Slides/SlideShowWindows 局部变量 + finally 释放。 | 可复用的确定性 temporary 所有权形状；不能改成链式调用。 |
| `PptCOM/PptCOM.cs:1988`、`:2022`、`:2034`、`:2044` | Next/Previous/Exit/SlideNavigation.Visible 当前仍有链式 COM 访问。 | 即使成员可用，也存在无法明确释放 temporary 的风险；用户已将该边界纳入修复。 |
| `PptCOM/PptCOM.cs:1658`、`:1732` | LaserPointerEnabled dynamic 后反射；PointerType 类型接口优先再 dynamic/反射。 | 应按 dynamic 优先和逐成员例外原则修正；激光/荧光缺失仅禁用该能力。 |
| `PptCOM/SlideShowSession.cs:345`、`:353` | guarded Exit 先重读 accessor HWND，再取 View、调用 Exit。 | 修改普通 accessor 后仍需统一窗口能力回退；识别能进而退出永远失败是不完整兼容。 |
| `Inkeys/IdtPlug-in.cpp:697` | descriptor HWND 为 0 时仅 `!sessionApi` 调 GetPptShow。 | 有新 sessionApi 的损坏设备无法利用旧窗口回退。应保留完整身份约束接回兼容路径。 |

### Office / PIA / WPS 版本不能混同

- 微软官方版本映射：Office2007=12.0，Office2010=14.0，Office2013=15.0，Office2016=16.0；见 [Microsoft Security Advisory 4053440 的版本表](https://learn.microsoft.com/en-us/security-updates/securityadvisories/2017/4053440)。年份、Office 主版本、Interop 程序集版本是不同维度。
- `PptCOM.csproj:11` 保持 .NET Framework4.0；`:45` 和 `:49` 引用 PIA15，`:47`、`:51` 为 EmbedInteropTypes=True。微软说明嵌入使用到的互操作定义后可不部署 PIA，但并没有把缺失 COM 成员、QI 或提供方支持凭空补齐：[Type equivalence and embedded interop types](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/type-equivalence-and-embedded-interop-types)。
- C# dynamic 是运行时解析语言能力，.NET Framework4.0 就提供；其使用不要求 Office2010 才能调用既有 COM。微软示例的 Office 前提可早至 2007：[How to access Office interop objects](https://learn.microsoft.com/en-us/dotnet/csharp/advanced-topics/interop/how-to-access-office-interop-objects)。该示例是 Excel/Word，不能代替 PowerPoint/WPP 验收。
- **WPP 是 WPS 演示提供方；WPS2013 不是 Microsoft Office2013/MSO15。** 不应读取一个兼容 Application.Version 就反推微软特性等级，也不能以类型库或包装库写着 3.0/9.0/15.0 推断真实 SDK 能力。
- WPS 当前官方 [Slide 对象](https://open.wps.cn/documents/app-integration-dev/wps365/client/wpsoffice/jsapi/wpp/Slide/obj)、[SlideIndex](https://open.wps.cn/documents/app-integration-dev/wps365/client/wpsoffice/jsapi/wpp/Slide/member/SlideIndex) 和 [SlideRange.SlideID](https://open.wps.cn/documents/app-integration-dev/wps365/client/wpsoffice/jsapi/wpp/SlideRange/member/SlideID) 描述相关属性，但这是当前 WPS365 JSAPI 文档，**不是 WPP2013 Windows COM SDK 的版本保证**。在线 WebOffice 同名 API 更不能作为本机 COM 证据。
- 本轮没有取得官方 WPP2013 COM 成员逐版本表；因此不写“WPP2013 已兼容”或“WPP2013 等价 MSO15”。验收必须记下完整产品/补丁/文件版本、位数、ProgID 和可用接口。

### HWND 为什么是可以论证的有限例外

本轮在 PowerShell 中先 LoadFrom `Office.dll`、再 LoadFrom 仓库 PIA15，仅读取 `GetCustomAttributesData()`，不创建 Application/RCW、不调用 Office。元数据摘要：

```text
Microsoft.Office.Interop.PowerPoint, Version=15.0.0.0
SlideShowWindow Guid=91493453-5A91-11CF-8700-00AA0060263B
HWND getter: DispId=2010, TypeLibFunc=1 (FRestricted)
View getter: DispId=2003, 无上述 FRestricted
SlideNavigation getter: DispId=2013
SlideShowView State getter: DispId=2006
SlideShowView PointerType getter: DispId=2005
SlideShowView 未在该 PIA 中声明 LaserPointerEnabled
```

- [微软 FUNCFLAGS 定义](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/ne-oaidl-funcflags) 明确 `FUNCFLAG_FRESTRICTED=1` 是宏语言不应访问的受限成员，不等同于普通 hidden。
- [微软 C# binder 源码 IDispatchComObject](https://github.com/dotnet/runtime/blob/main/src/libraries/Microsoft.CSharp/src/Microsoft/CSharp/RuntimeBinder/ComInterop/IDispatchComObject.cs) 的 `EnsureScanDefinedMethods` 扫描时跳过 `FUNCFLAG_FRESTRICTED`；证明不能假定 dynamic 普通成员查找总能发现 HWND。现代 runtime 源码仅用于解释机制，不把其全部行为当作 .NET Framework4.0 实测。
- 历史记录 `recognition-regression-state.md:39`、`:47`：真实 PowerPoint showCount=1，State=1，SlideIndex=1，SlideID=256；同一窗口 `InvokeMember("HWND")` 抛 `0x80020003`，类型接口 HWND=10227496、PID=37764；reader 仍给 `StableSlideIds/Valid`，但 HWND/PID=0。该证据来自既有历史探针，本轮未重跑，不是 Office2010/WPP2013 证据。
- 因此计划可将 HWND 作为证据明确的标量适配例外：先按 dynamic 能力尝试；受限/成员不可用再用有限类型适配；类型接口仍不可用则走同一绑定文稿/进程/放映窗口的 Win32 识别。每一级须区分 busy 与不支持，避免 busy 触发串窗或误退出。
- 不能借此将普通 FullName、Name、View、Slide、Slides、Count、SlideID 等整体退回反射。历史只证明上述 HWND 的窄差异；事件连接另有连接点/类型包装需要。

### 按功能的兼容矩阵（当前证据，不是验收通过表）

`有旧版文档`=成员年龄符合目标；`待实机`=产品全链路未验证；`按能力`=缺少该成员只能限制该功能。

| 功能/必要 COM 成员 | PowerPoint2007 / MSO12 | PowerPoint2010 / MSO14 | WPP/WPS Presentation2013 | 计划中的降级 / 不变量 |
| --- | --- | --- | --- | --- |
| 放映发现、当前页、总页数：ActivePresentation / SlideShowWindow / View.Slide.SlideIndex / Slides.Count | 既有支持声明；View/Slide 在 Office2003 文档有，整链待实机 | 无发现新版本硬门槛；待实机 | 既有声明；没有2013专属 COM 证据，待实机 | dynamic；缺事件用轮询；核心调用不可达不能假称仍正常 |
| 下一页/上一页/退出：View.Next / Previous / Exit | Office2003 文档列出；待实机 | 同左；待实机 | 待2013实机 | 每次取 View 局部并 finally 释放；按最终设计校验当前COM场次后对象定向执行；OS焦点/窗口操作仍需可信窗口 |
| 文稿身份：FullName / Name | 既有成员；本轮未做全链旧版验收 | 未发现新增版本要求；待实机 | 完整路径、未保存文稿差异待实机 | 不以任意标题绑定另一份文稿；缺稳定路径只允许明确的本进程身份 |
| 稳定页身份：Slides.Item(i).SlideID + 同次 SlideIndex | Office12 官方文档确认 SlideID；待实机 | 成员年龄符合；待实机 | 当前 WPS365 文档有，无法外推2013 | 缺失或非法拓扑用显式页序号；禁止伪造 ID/跨文稿恢复 |
| 生命周期/结束页：View.State / SlideShowWindows.Count | State 在 Office2003 文档有；待实机 | 同左；待实机 | State 数值/结束页形状待实机 | 缺 State 仍可用自洽 Slide；失败不推断结束；无事件重开有观测限制 |
| 放映 HWND/PID 和激活窗口 | HWND 受限成员风险；待实机 | 同左；待实机 | 是否支持 HWND/PIA 各异，待实机 | 有依据的有限适配 + 安全 Win32 回退；HWND/PID/唯一性仍严格验证 |
| 事件通知：SlideShowBegin/NextSlide/End/BeforeClose | 既有类型事件路径；待实机 | 同左；待实机 | 不能假设微软 PIA 事件对全部 WPS 有效 | 包装不可用/部分失败必须回滚并轮询，不能阻断核心识别/翻页 |
| 标准画笔识别/复位：PointerType | Office2003 文档有；待实机 | 同左；待实机 | 枚举/返回值差异待实机 | dynamic 优先；缺失只禁用批注接管，不阻断页码/画布 |
| 激光笔识别/退出：LaserPointerEnabled | 不能以当前文档证明2007支持该属性 | 没有取得2010该 COM 属性首发证据；UI有激光不等于属性有 | 未取得2013该 COM 属性保证 | 按能力；成员缺失不反复无理由反射；不把失败解释为放映结束 |
| 荧光笔接管 | 标准 PointerType 没有通用 Highlighter 保证 | 同左 | 可能为提供方扩展，待实机 | 仅提供方明确返回支持名称才识别；不能猜未知数值 |
| 幻灯片总览：SlideNavigation.Visible | 未查到官方2007成员保证；按能力、待实机 | 未查到官方2010成员保证；按能力、待实机 | 未查到官方2013 COM 保证 | 可选独立能力；缺失只该按钮无效果/不可用，不能阻断核心；取得 Navigation 要释放 |
| 关闭文稿/退出与打开下一份文稿 | 未做真实进程退出验收 | 未做真实进程退出验收 | WPS有历史特殊清理，未验收 | 引用和订阅确定性配平；不靠 GC/睡眠/杀进程通过；fake 仅验证记账 |

核心成员官方证据：

- [Office12 _Slide.SlideID](https://learn.microsoft.com/en-us/previous-versions/office/office-12/ff762136%28v%3Doffice.12%29)：SlideID 不随页重排改变，区别于 SlideIndex。
- [Office2003 SlideShowView 对象](https://learn.microsoft.com/en-us/previous-versions/office/developer/office-2003/aa221456%28v%3Doffice.11%29)：View、Slide、PointerType、State、Next/Previous/Exit 均列出。
- [Office2003 State](https://learn.microsoft.com/en-us/previous-versions/office/developer/office-2003/aa212990%28v%3Doffice.11%29)、[当前 PpSlideShowState 枚举](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.ppslideshowstate)：Running1/Paused2/Black3/White4/Done5。
- [PointerType](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slideshowview.pointertype)、[LaserPointerEnabled](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slideshowview.laserpointerenabled)、[SlideNavigation](https://learn.microsoft.com/en-us/office/vba/api/powerpoint.slideshowwindow.slidenavigation)：当前定义支持能力探测，后两者页面没有足以证明2007/2010/WPP2013首发版本的声明。

### “COM 损坏”必须拆成可测试的类别

| 类别 | 可能现象 | dynamic 的能力边界 / 计划期断言 |
| --- | --- | --- |
| PIA 托管部署缺失或版本差异 | 类型载入异常 | EmbedInteropTypes 可减少所需 PIA 部署；不等于 COM 本身正常。类型定义加载失败不应把仍可 dynamic 的普通读取串成必须路径。 |
| 提供方特定类型接口 QI 失败 | `E_NOINTERFACE`、cast失败，但 IDispatch 成员正常 | 本任务首要场景；常规功能目标可用，事件退轮询、HWND安全回退。无需每次常规读取先试 PIA。 |
| 事件连接点/事件元数据/Advise异常 | 属性可读，Begin/Next/End 不到或绑定异常 | core dynamic 可继续；须完整清除已经绑定的事件/连接，启用轮询。事件健康不能只靠 cast 成功。 |
| IDispatch 本来不提供 ITypeInfo | GetTypeInfoCount=0 或某些接口返回 E_NOINTERFACE | 可能存在仅靠名字/Invoke的路径，但要在目标 .NET4 binder 和真实 COM 替身验证；不能用普通 CLR fake 代表。 |
| 类型信息预期存在但注册损坏 | GetTypeInfoCount=1，而 GetTypeInfo 返回 TYPE_E_LIBNOTREGISTERED 等 | DLR 可能在访问普通属性前失败；不能承诺 dynamic 兜底全部 typelib 损坏。必须保留能力/错误分类而非静默声称可用。 |
| 个别成员缺失/受限 | DISP_E_UNKNOWNNAME / DISP_E_MEMBERNOTFOUND | 常规缺失按功能降级；HWND等确有证据的例外才用有限适配。页码等核心成员完全缺失不能伪造。 |
| Office暂忙 | 0x8001010A/0x800AC472/0x80010001 | 不是版本不支持或永久损坏；有界恢复，不能误报结束、乱切绑定或重新枚举全部能力。 |
| 激活/ROT不可达、权限隔离、注册彻底缺失 | CLASSNOTREG、ROT不可见、core IDispatch拿不到 | dynamic 无法制造不存在/不可达对象。单凭版本号无法修复，不能承诺相同功能。 |
| 核心 IDispatch GetIDsOfNames/Invoke 失效 | 大部分基础属性/操作都失败 | dynamic与反射通常共用下层Automation，互换语法不能修复；明确边界，不以反射重试假装成功。 |

[微软 IDispatch 总览](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/nn-oaidl-idispatch)、[GetTypeInfo](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/nf-oaidl-idispatch-gettypeinfo)、[GetIDsOfNames](https://learn.microsoft.com/en-us/windows/win32/api/oaidl/nf-oaidl-idispatch-getidsofnames) 分别定义接口/类型信息/名字映射，没有保证每个提供方在缺失类型库时照常执行。

更直接的第一方机制证据：[Microsoft.CSharp ComRuntimeHelpers 源码](https://github.com/dotnet/runtime/blob/main/src/libraries/Microsoft.CSharp/src/Microsoft/CSharp/RuntimeBinder/ComInterop/ComRuntimeHelpers.cs) 的 `GetITypeInfoFromIDispatch` 区分“不提供类型信息”和“预期有但注册异常”，后者抛错；[DLR 仓库对应实现](https://github.com/IronLanguages/dlr/blob/main/src/core/Microsoft.Dynamic/ComInterop/ComRuntimeHelpers.cs) 也保留该区分。这些是当前实现机制依据，**尚未取得完全一致的 .NET Framework4.0 源码/实际运行证据，故 .NET4 的细节列为待验证假设**。

### 后续隔离 COM 能力夹具（本轮仅规划）

实施阶段应在不破坏用户 Office 注册的前提下，让生产 dynamic 访问器消费真正的 COM IDispatch 替身。普通 CLR dictionary/reflection fake 不能覆盖以下差异。

1. 提供 IDispatch 和正常 ITypeInfo、基础 getter/Item/Next/Previous/Exit；拒绝 Office Application/SlideShowWindow/View 的类型 IID。证明“typed QI 失败”时生产 dynamic 仍能完成页码/拓扑/操作，且 HWND单独安全回退。
2. 同上但事件连接点/事件 IID/Advise 失败，包含订阅到一半再失败。验证准确解绑、无事件轮询持续工作。
3. IDispatch 正常但 GetTypeInfoCount=0；另测 Count=1/GetTypeInfo=E_NOINTERFACE；记录 .NET4 getter、Item和无参方法的真实表现，不能预填“都通过”。
4. IDispatch 名字/Invoke仍可用，但 Count=1/GetTypeInfo=TYPE_E_LIBNOTREGISTERED；这是 dynamic 是否会因类型信息失败的边界用例。失败是需报告的运行时限制，不能改变用户系统注册去模拟。
5. HWND 为受限/成员缺失，其他成员可用；再叠加 typed QI 失败。验证识别、缓存、受会话保护的 Exit 都消费同一个已验证窗口，不因 accessor 前后不一致而只能识别不能退出。
6. 去掉 SlideID、State、LaserPointerEnabled、SlideNavigation，分别验证只降级其依赖功能。测试 WPS风格 bool/整型返回以及未知枚举，不推断结束/荧光。
7. 同一COM identity重复返回的临时子对象、每个失败点、跨线程owner请求与清理交错，检查 COM服务器侧引用/事件计数和回调停止；不能仅检测托管对象是否 null。

这些夹具能证明生产访问和释放逻辑，不会证明真实 WPP2013/Office2010 进程是否干净退出，也不会证明它们实际暴露的接口符合替身。

### 本机只读提供方盘点

2026-10-18 使用 `RegistryKey.OpenBaseKey` 分别读取 HKCR 的 Registry64 / Registry32；未调用 ProgID→激活、GetActiveObject、ROT.GetObject 或任何 Office API。

| 检查 | 结果 | 结论范围 |
| --- | --- | --- |
| HKCR PowerPoint.Application（64/32视图） | CurVer=PowerPoint.Application.16；CLSID={91493441-5A91-11CF-8700-00AA0060263B} | 注册存在，不能证明它当前可用 |
| CLSID LocalServer32（64/32视图） | `C:\Program Files\Microsoft Office\Root\Office16\POWERPNT.EXE /AUTOMATION`（32视图引号略不同） | 两视图注册同一路径不代表两种位数都实测可用 |
| EXE FileVersion/ProductVersion | 16.0.20430.20182；CompanyName=Microsoft Corporation | 当前文件版本，不代表 Office2010 |
| EXE PE COFF Machine（只读取 DOS/PE header） | PE=0x00004550；Machine=0x8664 | 文件是 AMD64/x64；[微软PE规范](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format)确认该常量。未观察运行进程，不外推 ARM64客户到该 COM服务器已验收 |
| ClickToRun Configuration | VersionToReport=16.0.20430.20182；Platform=x64；ProductReleaseIds=OneNoteFreeRetail,O365ProPlusRetail | 安装配置声明 x64，与文件头一致；不是运行连接测试 |
| HKLM Office12/14/15 PowerPoint InstallRoot（64/32） | 均缺失；仅64视图Office16有路径 | 未发现已注册旧版；不扫描全盘推断所有便携/未注册安装不存在 |
| KWPP.Application/WPP.Application（64/32） | 均缺失 | 未发现注册的WPP提供方，不能在本机做该版本真实验收 |
| 常见 Program Files Kingsoft目录 | 两处均未发现 | 仅辅助证据，不等于完成全盘排查 |
| HKCU WPP App Paths 补充读取 | runner返回 Requested registry access is not allowed | 没有升级权限/改注册表；HKCU补充范围不完整，不据此断言没有任何WPS文件 |

本轮另外尝试仅加载 PIA 做元数据读取时，最初未预先加载 Office.dll 导致 PowerShell 的 GetTypes 依赖加载失败；随后同进程先加载仓库 Office.dll 后已成功读取元数据。这是研究工具依赖解析问题，不是产品部署或用户 COM 损坏证据。

### 后续真实旧版验收安排

须在用户批准实现并明确授权 GUI/Office 验收之后执行；本轮没有获得该 GUI 授权。

| 提供方 | 最低必需覆盖 | 必须记录 |
| --- | --- | --- |
| PowerPoint2007/MSO12 | 保留现有声明：当前页/总数、翻页/退出、无可选属性、关闭后再打开下一份 | 完整文件版本、安装方式/补丁、Office位数、Windows及Inkeys架构 |
| PowerPoint2010/MSO14 | 与2007相同，加SlideID稳定恢复、State黑白屏/结束页、事件及轮询 | 同上；无模板测试替代真实COM能力 |
| WPP/WPS Presentation2013 | 完整ProgID/类型接口情况、事件失败轮询、SlideID/State能力、结束重开、退出进程/再开文稿 | 完整WPS版本/发行版/补丁/位数、提供方名称、实际可用属性和错误码；不标MSO15 |
| 当前Office16 | 已有健康路径与类型QI/事件隔离场景；作为回归对照 | 文件/运行版本、客户架构、服务器架构分别记录 |

场景至少包括：Inkeys启动前已在放映；动画步进/实际换页；最后页/结束页；黑白屏/暂停；Esc/Office退出/Inkeys显式退出；同文稿结束重开；两份文稿/同名不同路径；SlideID/State可选能力缺失；事件绑定失败轮询；短暂/持续busy；切文稿和退出时在途命令；用户关闭PowerPoint/WPP后进程/事件连接终止且下一份文稿立即可打开。

退出验收不能以杀进程、强制GC、等待几秒/几十秒自然消失、或仅看到窗口消失判通过。应与用户直接使用Office/WPP的基线区分，核对Inkeys可控引用/事件连接全部撤销；外部插件/服务器自身行为如仍阻碍退出，应独立归因并保留证据。

## Related specs

- `.trellis/spec/ppt-interop/index.md` 的既有版本声明和逐功能实测边界仍有效；用户本轮已明确保留PowerPoint2007、要求调查2010/WPP2013，不需再猜支持范围。
- `.trellis/spec/ppt-interop/com-contract.md:51` 的常规 InvokeMember 要求与用户 dynamic 优先要求冲突；实现获批时需由主会话/规范流程同步纠正，本研究员未修改。
- `.trellis/spec/ppt-interop/com-contract.md:52` 与 `native-session-ui3.md:22` 的 HWND 类型接口现状有历史证据，但尚未满足类型QI失败时的安全回退目标。应将例外限定到确有证据的能力，统一识别与命令验证。
- `com-contract.md:53` 的子到父 temporary 释放、纯值缓存和无链式 COM 原则应保留并扩展到全部既有命令/Navigation边界；禁止以 GC 作为释放保证。
- `native-session-ui3.md:116`—`:127` 的批注可选能力原则与本次旧版兼容矩阵一致；缺可选成员不应阻断主联动。

## Caveats / Not Found

- 未做任何本轮真实COM访问；本机注册/EXE元数据只能盘点候选提供方，不能证明其Automation或所有位数/客户架构可用。
- 未取得官方WPP2013 Windows COM SDK逐成员版本表；现代WPS365/在线API文档不能替代。
- 未取得官方对LaserPointerEnabled/SlideNavigation在PowerPoint2007/2010的首发版本保证；不把UI有某功能等同于COM成员可读。不擅自断言旧版本一定支持或一定缺失。
- HWND FRestricted 为仓库PIA15元数据直接确认；现代C# binder源码与历史真实探针提供机制/经验依据；.NET4实际dynamic失败形状仍需获批后的隔离COM夹具验证。
- 严重typelib/ITypeInfo注册损坏时dynamic能否运行依赖具体HRESULT/提供方/.NET运行时；不能保证所有“COM损坏”都可用。主要修复目标应明确限定为核心dynamic实际仍可用的部分损坏。
- 未执行构建/测试，因为本轮只调查和规划，不生成夹具、产物或产品改动。
- `trellis-start` 不在本子会话可见skill目录；已按注入的Active task读取workflow、相关spec和任务PRD。未创建/启动/切换任务，也未读取implement/check上下文清单。
