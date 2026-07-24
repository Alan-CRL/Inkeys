# Rendering and UI

本页区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。图形实现是按窗口分流的；“仓库用了 D3D11”不等于所有窗口或 ImGui 都使用 D3D11。

## UI 路由与后端分工

| 区域 | 当前选择关系 | 图形/呈现链 | 直接证据 |
| --- | --- | --- | --- |
| `Inkeys.UI.Bar` | `【直接确认】` `IdtMain.cpp::wWinMain` 仅在 `useInkeys3UI` 为 true 时启动 `Inkeys::UI::Bar::Initialization` | D3D11 WARP 提供 DXGI/D2D device；Bar 用 D2D/DWrite 绘制，经 GDI interop 和 `UpdateLayeredWindowIndirect` 呈现 | `IdtMain.cpp`、`IdtD2DPreparation.cpp::D2DStarup`、`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp::BarUISetClass::Rendering` |
| 传统 `IdtFloating` | `【直接确认】` 同一分支在 `useInkeys3UI` 为 false 时启动 `floating_main`；`SetListStruct::Experimental.Inkeys3.UI3` 的源码默认值为 false | HiEasyX/EasyX、GDI/GDI+、分层窗口 | `IdtConfiguration.h`、`IdtMain.cpp::wWinMain`、`IdtFloating.cpp` |
| 设置窗口 | `【直接确认】` 当前主工程编译的唯一 ImGui renderer 是 DX9 | Dear ImGui Win32 + Direct3D 9 HAL | `Setting.Base.cppm::CreateDeviceD3D`、`Setting.cpp` 中 `ImGui_ImplDX9_*`、`Inkeys.vcxproj` |
| 主画板 | `【直接确认】` 两套悬浮栏分支共用的墨迹窗口 | HiEasyX/EasyX `IMAGE`、GDI+ 笔画、软件合成、分层窗口 | `IdtDrawpad.cpp::DrawpadDrawing`、`IdtImage.cpp` |
| PPT 控件 | `【直接确认】` 放映联动的特定窗口 | HiEasyX/EasyX 背景表面 + D2D DC render target/DWrite + GDI | `IdtPlug-in.cpp::PptUI` |
| 冻结帧、放大镜等 | `【直接确认】` 独立传统工具窗口 | 以各自现有 GDI/EasyX 路径为准 | `IdtFreezeFrame.cpp`、`IdtMagnification.cpp` |

`【待确认】` `Experimental.Inkeys3.UI3` 可从旧配置文件读取并覆盖源码默认值，因此“源码默认进入 `IdtFloating`”不等于“当前发布包一定默认旧栏”。维护者尚需确认发布时的 `opt/deploy.json` 默认内容、回退策略和旧栏淘汰计划。

`【历史/兼容】` `IdtFloating` 仍是可执行分支，不能称为已弃用；`Inkeys.UI.Bar` 的命名和实验开关也不足以证明它已是唯一正式主路径。

## D3D11 WARP、D2D 与 DWrite

`【直接确认】` `Inkeys/IdtMain.cpp::wWinMain` 在选择新旧悬浮栏之前调用 `D2DStarup()`。`Inkeys/IdtD2DPreparation.cpp::D2DStarup` 依次：

1. 创建 multithreaded `ID2D1Factory1`；
2. 创建 shared `IDWriteFactory`；
3. 以 `D3D_DRIVER_TYPE_WARP`、`D3D11_CREATE_DEVICE_BGRA_SUPPORT` 和 feature level 11.1/11.0 创建 D3D11 device；
4. 取得 `IDXGIDevice`；
5. 由 D2D factory 创建 `ID2D1Device`。

共享对象使用 `Microsoft::WRL::ComPtr`。初始化失败路径会写日志并 reset 已创建对象。

`【直接确认】` 消费者并不相同：

- `d2dDevice_WARP` 在本轮扫描中由 `Bar.Main.cpp::BarUISetClass::Rendering` 用来创建 D2D device context；
- `d2dFactory1`、`dWriteFactory1` 也被 `IdtPlug-in.cpp::PptUI` 用于 D2D DC target 和文字，不依赖 Bar 是否启用；
- 设置窗口没有使用这条 device 链，而是独立 D3D9 device；
- 主画板不是 D2D target。

`【合理推断】` 修改现有 Bar/PPT 图形初始化时应先沿用共享 factory/device 的现有生命周期。新增完全独立设备是否合适属于架构决策，不能由“当前共享”自动升级成永久禁令。

`【待确认；风险观察】` `IdtD2DPreparation.cpp` 定义了 `D2DShutdown()`，全仓静态搜索未找到调用点。本轮未做运行时退出验证，因此只记录为清理契约待确认，不称为资源泄漏或已确认缺陷。

## `Inkeys.UI.Bar`

`【直接确认】` `Inkeys/Inkeys/UI/Bar/` 按 `Bar.State`、`Bar.Atomic`、`Bar.Theme`、`Bar.UI`、`Bar.RenderingAttribute`、`Bar.Bottom`/`Bar.Buttom`、`Bar.Main`、`Bar.Zoom`、`Bar.Format` 等文件拆分状态、主题、布局/动画、渲染和窗口逻辑。

`Bar.Main.cpp::BarUISetClass::Rendering` 从共享 D2D device 创建 device context，并创建 GDI-compatible、BGRA premultiplied target bitmap；绘制后通过 `ID2D1GdiInteropRenderTarget::GetDC` 和 `UpdateLayeredWindowIndirect` 提交计算出的脏区。代码存在静止等待和约 60 FPS 的节奏控制。

以下仅是对现有 Bar 的 `【合理推断】` 修改约束，不适用于所有窗口：

- 保持 target/current 动画状态与最终绘制值的现有分工；
- 状态变化需要触发现有渲染唤醒/脏区路径，静止时避免无意义刷新；
- 维持 premultiplied alpha 语义；
- `GetDC/ReleaseDC`、`BeginDraw/EndDraw` 在成功与失败路径成对；
- 不在没有线程安全依据时跨线程传递 device context/target。

### UI3 第三鼠标光休眠契约

#### 1. Scope / Trigger

当修改 UI3 Bar 的第三鼠标光、全局鼠标跟踪或画布落笔通知时，必须保持 `Dormant → Inside → Grace` 状态机；传统 `IdtFloating` 不属于该契约。Draw2 的鼠标、笔和触摸在统一绘制线程派发边界触发休眠；后续 Draw3 应复用同一通知接口。

#### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void NotifyCanvasDrawingStarted();
}
~~~

画布只能调用该通知接口，不得从画布线程直接修改 Bar、D2D 或 Raw Input 状态。

#### 3. Contracts

- `Dormant`：第三光源目标透明度为 0，鼠标 Raw Input 必须注销；仅 UI3 窗口自然收到真实 `WM_MOUSEMOVE` 才能进入 `Inside`。
- `Inside`：注册鼠标 `RIDEV_INPUTSINK`；首次离开实际接收消息的窗口区域时进入 `Grace`，并记录 `GetTickCount64() + 5000` 的绝对截止时间。
- `Grace`：区域外移动不得重置截止时间。第三光源使用独立的 `240 × barStyle.zoom` 径向渐变；已发布 UI 外框只用于判断光圈是否可能命中 UI，从而裁剪渲染唤醒，不得作为全局亮度乘数。
- 同一控件的同一边框像素上，第三光源贡献只由光标到该像素的距离、生命周期强度和控件固定比例决定；光标是否位于接受消息区域、主栏或其他可见区域内不得改变该贡献。第一光源可独立影响最终合成结果，不属于该一致性契约。
- `Grace → Inside`：重新进入实际接收消息区域时取消定时器；仅回到 240px 邻域不能从 `Dormant` 唤醒。
- `Grace → Dormant`：绝对截止时间到达，或画布开始真实绘制时，注销 Raw Input 并从当前强度平滑淡出。落笔时若光标仍在 UI3 接收区，区域内后续移动不得重新激活；必须先收到离开，再由下一次自然进入激活。
- 5 秒等待使用窗口定时器，不得新增轮询线程或靠持续渲染计时。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `RegisterRawInputDevices` 注册失败 | 第三光源保持隐藏；错误至多记录一次，不影响主光和 UI 交互 |
| `RIDEV_REMOVE` 注销失败 | 逻辑状态仍进入 `Dormant`，迟到的 `WM_INPUT` 必须被忽略 |
| 窗口定时器创建失败 | 立即进入 `Dormant`，不得无限保留全局跟踪 |
| 动画关闭 | 立即隐藏第三光源并请求休眠 |
| 触摸模拟鼠标消息 | 不得激活第三光源；画布休眠仅由 Draw2 统一落笔派发边界通知，不由模拟鼠标消息重复通知 |

#### 5. Good / Base / Bad Cases

- Good：离开 UI 后在外部持续移动，5 秒截止时间保持不变；第三光源按 240px × zoom 径向渐变连续归零，光圈离开全部可见外框后不再因位置变化唤醒渲染。
- Base：宽限期内返回 UI，取消休眠并从当前透明度继续淡入。
- Bad：把光标到主栏、绘制属性栏等任意区域的最近距离乘到所有控件的第三光源强度上；这会让其他区域为当前边框“托底”，造成等距离位置亮度不同。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证 UI 外启动、进入 UI、离开后 5 秒内返回、超过 240px、5 秒超时、休眠后仅靠近 240px、Draw2 鼠标/笔/触摸落笔、落笔时仍位于接收区和动画关闭。
- 对同一边框像素分别从接受消息区域内外取等距离光标位置，隔离第一光源后确认第三光源贡献一致。
- 性能验证至少比较 `Dormant` 与持续全局移动时的 CPU；`Dormant` 中不得出现由第三光源导致的持续渲染唤醒。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：区域外每次移动都会把休眠延后 5 秒。
deadline = GetTickCount64() + 5000;

// Correct：只在 Inside → Grace 的首次转换设置绝对截止时间。
if (state == TrackingState::Inside)
{
	state = TrackingState::Grace;
	deadline = GetTickCount64() + 5000;
}
~~~

~~~cpp
// Wrong：任意 UI 的最近距离会同时改变所有边框的第三光源亮度。
cursorIntensity = lifecycleIntensity * nearestVisibleRegionIntensity;

// Correct：区域距离只裁剪唤醒；第三光源贡献由自身 240px 径向画刷决定。
cursorIntensity = lifecycleIntensity * controlIntensityScale;
cursorRadius = 240.0 * zoom;
~~~

### UI3 边缘光影实验开关契约

#### 1. Scope / Trigger

修改 UI3 边缘点光、第三鼠标光的设置或 Inkeys3 配置时，必须同时核对 Setting、`Inkeys.Other.Config` schema、Bar 运行时门禁与启动同步；传统 `IdtFloating` 不在该契约内。

#### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void SetEdgeLightingOptions(bool enable, bool dynamic);
}
~~~

持久化路径为：

~~~text
Experimental.Inkeys3.UI3.EdgeLighting.Enable  : bool = true
Experimental.Inkeys3.UI3.EdgeLighting.Dynamic : bool = true
~~~

#### 3. Contracts

- `Enable=false`：第一主光、第三鼠标光和 Gaussian 柔光均不绘制；基础灰边继续绘制；停止仅服务于光影的动画推进，并请求第三光源进入 Dormant、注销 Raw Input。
- `Enable=true, Dynamic=false`：第一主光和 Gaussian 柔光保持；只有第三鼠标光关闭，禁止注册/处理其 Raw Input。切换关闭时可以沿既有 300ms 淡出收尾。
- `Enable=true, Dynamic=true`：保持第三光源 `Dormant / Inside / Grace` 状态机；重新开启不主动全局取鼠标，等待自然进入 UI3。
- Setting 只在 UI3 开启时显示总开关，只在总开关开启时显示动态开关；隐藏动态项不得覆盖其持久化值。
- schema 是字段声明、默认值和 JSON 读写的唯一来源；不得在 `Other.Config.cpp` 为两个 bool 添加特判。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| 旧配置缺少新字段 | schema 默认值均为 `true`，升级后视觉保持不变 |
| 关闭任一动态门禁 | 立即阻止新的激活/位置处理，并向 Bar 窗口投递既有休眠消息 |
| 配置写入失败 | 本次运行时状态仍即时生效；不得直接从 Setting 线程修改 D2D 或 Raw Input |
| 总开关关闭期间工具或颜色变化 | 不因第一光源位置、颜色或过渡继续产生光影专用渲染帧 |

#### 5. Good / Base / Bad Cases

- Good：关闭动态开关后主光仍可见，第三光源完成收尾后不再注册 Raw Input。
- Base：关闭总开关后只剩基础灰边；重新开启后恢复主光，并保留此前动态开关选择。
- Bad：把动态开关乘到全部光影或基础灰边上，导致关闭第三光源时第一主光也消失。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证四种组合：总开关关、总开关开且动态关、两者全开、动态关后重启。
- 检查 `main.json` 两个路径、旧配置缺失字段默认值、设置卡片条件显示与容器高度。
- 在动态关闭和总开关关闭时观察 Raw Input 注销及静止/移动 CPU，确认第三光源不再唤醒渲染。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：动态开关错误地关闭所有边缘光影。
drawPrimaryLight = edgeLightingEnabled && dynamicEdgeLightingEnabled;

// Correct：动态开关只进入第三光源的激活与可见性条件。
drawPrimaryLight = edgeLightingEnabled;
desiredCursorLightVisible = edgeLightingEnabled && dynamicEdgeLightingEnabled
	&& cursorInputAvailable;
~~~

### UI3 动画批次加入与关键帧中点

`【直接确认；设计约定】` `BarUiTimelineClass::CanJoin(double maxProgress = 0.5)` 是主栏布局变化和绘制属性加入主栏批次的统一判断入口；无效参数同样回退到 `0.5`，边界判断为 `GetProgress() <= maxProgress`。

| 线性时间轴进度 | 契约 |
| --- | --- |
| `progress <= 0.5` | 新目标可以复用当前批次的剩余时长，并继续共享原截止时间。 |
| `progress > 0.5` | 新目标必须创建完整的新批次；旧的单中间关键帧不再继承。 |

该边界与现有唯一中间关键帧的 `0.5` 时刻一致。若重新把加入阈值设到中点之后，会产生“关键帧已经过去、但新目标仍沿用旧批次语义”的区间，可能表现为重复收拢或后半程被过度压缩。修改批次阈值时应搜索全部 `CanJoin()` 调用，并手工覆盖中点前、中点附近和中点后的目标变化。

## 设置窗口与 ImGui

`【直接确认】`：

- `Setting.Base.cppm::CreateDeviceD3D` 调用 `Direct3DCreate9` 并创建 D3D9 HAL device；
- `Setting.cpp` 调用 `ImGui_ImplWin32_Init`、`ImGui_ImplDX9_Init/NewFrame/RenderDrawData/Shutdown`，处理 `D3DERR_DEVICELOST` 和 device reset；
- `Inkeys/Inkeys.vcxproj` 编译 `additional/imgui/imgui_impl_win32.cpp` 与 `imgui_impl_dx9.cpp`。

`【历史/兼容；非当前产品路径】` `Inkeys/additional/imgui/` 也随附 `imgui_impl_dx11.cpp/.h`，但 vcxproj 没有编译它，第一方代码也未找到 `ImGui_ImplDX11_*` 调用。因此应写成“DX11 backend 随附但当前未接入”，不能写成“仓库不存在 DX11 实现”，也不能把它与设置窗口当前后端混为一谈。

`【合理推断】` 设置 UI 的局部改动应复用 `Setting.Widgets`、`Setting.Wrap` 及现有字体/纹理和 reset 路径。切换到 ImGui DX11 是完整后端迁移任务，需要同时设计 device、纹理、lost/reset、线程和退出行为；它不是绝对禁止项，也不应夹带在普通设置功能中。

## EasyX、GDI/GDI+ 与特定窗口

`【直接确认】`：

- `IdtDrawpad.cpp` 使用 HiEasyX/EasyX `IMAGE`、GDI+ 曲线及软件合成，`DrawpadDrawing` 组合基础画布与活动笔画后更新分层窗口；
- `IdtFloating.cpp` 使用 EasyX/GDI+ 实现传统悬浮栏；
- `IdtPlug-in.cpp::PptUI` 在 HiEasyX/EasyX 背景上使用 D2D DC target 绘制 PPT 控件；
- `IdtFreezeFrame.cpp`、`IdtMagnification.cpp` 各自保留传统窗口/图像链。

这些是并存的当前实现，不是简单的“新后端取代旧后端”。修改前必须先确认目标窗口，不得把 Bar 的 D2D device context、设置的 ImGui DX9 或画板的 `IMAGE` 生命周期规则互相套用。

## Win32、DPI 与坐标

`【直接确认】` 程序有多个窗口和消息/渲染线程；多处使用 layered、transparent、no-activate/topmost 语义。`IdtWindow.cpp` 还维护窗口可见性、style 和置顶状态。`IdtMain.cpp`、`IdtDisplayManagement.cpp` 初始化系统版本、DPI 和显示器状态。

`【合理推断】` 修改尺寸、命中或窗口显示行为时，应追踪目标窗口实际使用的逻辑/物理坐标、显示器原点、DPI 缩放、分层窗口脏区及 mouse/touch/RTS 坐标转换；不要仅在创建点修改 style 后假设维护线程不会覆盖它。

## 建议验证范围

以下是由当前多后端结构推导的建议，不代表仓库已有正式测试政策：

- 目标窗口创建、显示/隐藏、缩放、透明边缘和退出；
- Bar 的脏区、静止 CPU 与 D2D 失败路径；
- 设置窗口的 D3D9 device lost/reset 与纹理重建；
- 画板的基础层、活动笔画、撤销/恢复和 PPT 换页合成；
- `IdtFloating` 与 Bar 两条配置分支分别验证，直到维护者确认正式主路径。
