# Rendering and UI

本页区分 `【直接确认】`、`【合理推断】`、`【待确认】` 和 `【历史/兼容】`。图形实现是按窗口分流的；“仓库用了 D3D11”不等于所有窗口或 ImGui 都使用 D3D11。

## UI 路由与后端分工

| 区域 | 当前选择关系 | 图形/呈现链 | 直接证据 |
| --- | --- | --- | --- |
| `Inkeys.UI.Bar` | `【直接确认】` `IdtMain.cpp::wWinMain` 仅在 `useInkeys3UI` 为 true 时启动 `Inkeys::UI::Bar::Initialization` | 默认由共享 D3D11 WARP epoch 提供 DXGI/D2D device；Bar 用独立 D2D device context/DWrite 绘制，经 GDI interop 和 `UpdateLayeredWindowIndirect` 呈现 | `IdtMain.cpp`、`IdtD2DPreparation.cpp::D2DStarup`、`Inkeys/Inkeys/UI/Bar/Bar.Main.cpp::BarUISetClass::Rendering` |
| 传统 `IdtFloating` | `【直接确认】` 同一分支在 `useInkeys3UI` 为 false 时启动 `floating_main`；`SetListStruct::Experimental.Inkeys3.UI3` 的源码默认值为 false | HiEasyX/EasyX、GDI/GDI+、分层窗口 | `IdtConfiguration.h`、`IdtMain.cpp::wWinMain`、`IdtFloating.cpp` |
| 设置窗口 | `【直接确认】` 当前主工程编译的唯一 ImGui renderer 是 DX11 | Dear ImGui Win32 + 独立 hardware D3D11 device/context、discard swap chain、RTV | `Setting.Base.cppm::CreateDeviceD3D`、`Setting.cpp` 中 `ImGui_ImplDX11_*`、`Inkeys.vcxproj` |
| 主画板 | `【直接确认】` 两套悬浮栏分支共用的墨迹窗口 | HiEasyX/EasyX `IMAGE`、GDI+ 笔画、软件合成、分层窗口 | `IdtDrawpad.cpp::DrawpadDrawing`、`IdtImage.cpp` |
| PPT 控件 | `【直接确认】` 放映联动的特定窗口 | HiEasyX/EasyX 背景表面 + D2D DC render target/DWrite + GDI | `IdtPlug-in.cpp::PptUI` |
| 冻结帧、放大镜等 | `【直接确认】` 独立传统工具窗口 | 以各自现有 GDI/EasyX 路径为准 | `IdtFreezeFrame.cpp`、`IdtMagnification.cpp` |

`【待确认】` `Experimental.Inkeys3.UI3` 可从旧配置文件读取并覆盖源码默认值，因此“源码默认进入 `IdtFloating`”不等于“当前发布包一定默认旧栏”。维护者尚需确认发布时的 `opt/deploy.json` 默认内容、回退策略和旧栏淘汰计划。

`【历史/兼容】` `IdtFloating` 仍是可执行分支，不能称为已弃用；`Inkeys.UI.Bar` 的命名和实验开关也不足以证明它已是唯一正式主路径。

## D3D11 WARP、D2D 与 DWrite

`【直接确认】` `Inkeys/IdtMain.cpp::wWinMain` 在选择新旧悬浮栏之前调用 `D2DStarup()`。`Inkeys/IdtD2DPreparation.cpp::D2DStarup` 依次：

1. 创建 multithreaded `ID2D1Factory1`；
2. 创建 shared `IDWriteFactory`；
3. 默认以 `D3D_DRIVER_TYPE_WARP`、`D3D11_CREATE_DEVICE_BGRA_SUPPORT` 和 feature level 11.1/11.0 创建 D3D11 device；Windows 7 运行时以 `E_INVALIDARG` 拒绝包含 11.1 的列表时仅重试 11.0；
4. 取得 `IDXGIDevice`；
5. 由 D2D factory 创建 `ID2D1Device`。

共享对象使用 `Microsoft::WRL::ComPtr`。默认 epoch 为 WARP；`PrepareUi3RenderBackend`/`CommitPreparedUi3RenderBackend` 为以后显式选择 Hardware 提供帧边界切换入口。初始化失败路径会写日志并 reset 已创建对象。

`【直接确认】` 消费者并不相同：

- `d2dDevice_UI3` 和 `GetUi3RenderDeviceEpoch()` 在本轮扫描中由 `Bar.Main.cpp::BarUISetClass::Rendering` 用来创建每客户端独立的 D2D device context；
- `d2dFactory1`、`dWriteFactory1` 也被 `IdtPlug-in.cpp::PptUI` 用于 D2D DC target 和文字，不依赖 Bar 是否启用；
- 设置窗口没有使用这条 device 链，而是独立 hardware D3D11 device/context 和 window swap chain；
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

### UI3 共享设备、整帧租约与光影缓存契约

#### 1. Scope / Trigger

新增或修改 UI3 Bar、PptBar、Setting、白板等使用同一 D3D11.1/D2D1.1 device 的客户端，或修改 WARP/Hardware 选择、Bar 光影、脏区与分层窗口提交时，必须遵守本节。该契约不把传统 `IdtFloating`、主画板 EasyX 表面或当前 ImGui DX9 设置窗口自动迁入 UI3。

#### 2. Signatures

~~~cpp
enum class Ui3RenderBackend : unsigned char { Warp, Hardware };
enum class Ui3RenderPriority : unsigned char { Interactive, Cosmetic };

Ui3RenderDeviceEpoch GetUi3RenderDeviceEpoch();
Ui3RenderPass AcquireUi3RenderPass(Ui3RenderPriority priority);
HRESULT PrepareUi3RenderBackend(Ui3RenderBackend backend);
bool CommitPreparedUi3RenderBackend();
~~~

每个 epoch 至少发布 `backend`、单调递增的 `generation`、实际 `featureLevel`、`ID3D11Device`、可选 `ID3D11Device1` 与 `ID2D1Device`。`Ui3RenderPass` 是 move-only RAII 租约。

#### 3. Contracts

- 启动默认创建 WARP epoch。Hardware 只能显式准备；准备过程在当前 epoch 旁路创建完整 device，失败时不得破坏正在使用的 WARP。
- 发布新 epoch 前必须取得 `Interactive` 整帧租约。客户端也必须先取得租约，再读取 epoch，并在 `generation` 变化时于本帧 `BeginDraw` 前重建自己的 device context、target、GDI interop、brush/effect/mask 等设备相关资源。
- 共享的是 D3D/D2D device，不共享客户端 device context 或 target。租约覆盖该客户端的完整绘制和呈现区间，至少包括资源检查、`BeginDraw`、D2D 命令、`GetDC/ReleaseDC`、`UpdateLayeredWindowIndirect` 与 `EndDraw`，禁止不同 UI3 客户端的帧交错。
- `Interactive` 帧可以等待租约；`Cosmetic` 帧在有交互等待者或设备正忙时必须跳帧，并保持正常帧率节流，不能自旋重试。开始真实绘图后，Bar 光影帧属于可牺牲的装饰工作。
- Bar 主帧只保留一组 `BeginDraw/EndDraw`，`GetDC` 已承担必要提交，前面不得再调用显式 `Flush`。Windows 7 Platform Update 路径在 `GetDC` 前必须弹出所有 clip/layer。
- 动态光只长期缓存颜色停靠点/画刷和几何的 A8 预模糊遮罩；画刷位置、半径和透明度每帧更新。禁止缓存快速变化的最终光影帧或冻结布局状态。
- A8 遮罩按几何参数量化且有容量上限。生成时使用同一 D2D device 上的专用 device context，先用一组 `BeginDraw/EndDraw` 写 source target，再把 source 作为 Gaussian 输入，用第二组 `BeginDraw/EndDraw` 写 output；禁止在同一 draw span 中把仍绑定为 target 的 bitmap 当作输入。
- 稳态帧不得创建 Gaussian effect、command list、渐变停靠点、solid brush 或重新生成已有遮罩。`FillOpacityMask` 前临时切为 `D2D1_ANTIALIAS_MODE_ALIASED`，结束后恢复。
- 光源与控件扩展边界不相交时必须裁剪该光源的 diffuse/hard-light 绘制；D2D 全局 dirty clip 和 layered-window dirty rect 必须使用旧边界与新边界的并集。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| Windows 7 运行时以 `E_INVALIDARG` 拒绝 feature level 11.1 列表 | 只用 11.0 重试；仍失败则保留初始化失败语义 |
| Hardware 准备失败 | 当前 WARP epoch 和全部客户端资源保持可用；不得发布半成品 |
| `generation` 变化后客户端资源重建失败 | 跳过该帧；同一 generation 错误限频，不能用旧 device 的资源向新 epoch 提交 |
| 装饰帧租约竞争失败 | 直接跳过并按目标 FPS 等待，不得忙循环 |
| A8、Effect 或遮罩专用 context 失败 | 本设备会话停用 diffuse mask；保留基础灰边和硬光，不得退回逐帧实时 Gaussian 或逐帧重试 |
| 主 `EndDraw` 返回 `D2DERR_RECREATE_TARGET` | 丢弃客户端设备资源并在下一帧按当前 epoch 重建 |
| 主 `EndDraw` 暴露刚创建遮罩的延迟错误 | 清空遮罩缓存并将本设备会话标为不可用，避免错误循环 |

#### 5. Good / Base / Bad Cases

- Good：WARP 中展开属性栏时，遮罩仅在新几何首次出现时生成；稳态帧复用 A8 mask、gradient brush 和 solid brush，Bar 主上下文每帧仅一次提交。
- Base：后台准备 Hardware 成功，帧间取得整帧租约并发布新 generation；Bar 下一帧先重建全部资源，再开始绘制。
- Bad：切换全局 device 指针后让旧 Bar context 继续一帧，或为避免重建而跨 device 复用 bitmap/brush；这会造成设备域错配、空白帧或设备丢失错误。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`，必须使用 ARM64 host MSBuild。
- 在 WARP 上分别测光影关、仅主光、主光+动态光：属性栏展开/收起、主栏状态切换、鼠标第三光和长时间静止；记录 CPU、帧时间、遮罩 cache miss 和提交次数。
- 在 Windows 7 SP1 + KB2670838 实机验证 feature level 回退、A8 target、Gaussian、`FillOpacityMask`、clip 栈为空时的 `GetDC` 及 layered-window 脏区无残影。
- 在支持设备上循环执行 WARP → Hardware → WARP 帧边界切换，覆盖动画中、装饰帧竞争、资源重建失败与 Hardware 准备失败；断言旧 epoch 在发布前始终可用。
- 后续每接入一个共享设备客户端，都要并发触发其交互与 Bar 装饰帧，断言帧串行、交互优先、无自旋和跨 device 资源复用。
- 视觉对比基础灰边、硬光、圆角九宫格遮罩接缝和超椭圆量化伸缩；允许经产品确认的轻微 diffuse 像素差异，不允许边缘断裂或残影。

#### 7. Wrong vs Correct

~~~cpp
// Wrong：先读取 epoch，等待租约期间 epoch 可能已经切换。
auto epoch = GetUi3RenderDeviceEpoch();
auto pass = AcquireUi3RenderPass(Ui3RenderPriority::Interactive);
DrawWith(epoch);

// Correct：租约内读取 epoch，并在 BeginDraw 前处理 generation。
auto pass = AcquireUi3RenderPass(priority);
if (!pass) return;
auto epoch = GetUi3RenderDeviceEpoch();
if (epoch.generation != localGeneration) RecreateClientResources(epoch);
BeginDrawAndPresent();
~~~

~~~cpp
// Wrong：刚把 bitmap 设为 target，就在同一 BeginDraw 中作为 effect input。
context->SetTarget(source);
context->BeginDraw();
DrawSource();
effect->SetInput(0, source);
context->DrawImage(effect);

// Correct：先结束 source 提交，再在独立 draw span 中生成 output。
maskContext->SetTarget(source);
maskContext->BeginDraw();
DrawSource();
maskContext->EndDraw();
maskContext->SetTarget(nullptr);
effect->SetInput(0, source);
maskContext->SetTarget(output);
maskContext->BeginDraw();
maskContext->DrawImage(effect);
maskContext->EndDraw();
~~~

### UI3 第三鼠标光休眠契约

#### 1. Scope / Trigger

当修改 UI3 Bar 的第三鼠标光、全局鼠标跟踪或画布落笔通知时，必须保持 `Dormant → Inside → Grace` 状态机；传统 `IdtFloating` 不属于该契约。Draw2 的鼠标、笔和触摸在统一绘制线程派发边界触发休眠；后续 Draw3 应复用同一通知接口。

#### 2. Signatures

~~~cpp
namespace Inkeys::UI::Bar
{
	export void NotifyCanvasDrawingStarted();
	export void NotifyCanvasDrawingEnded();
}
~~~

画布只能成对调用通知接口，不得从画布线程直接修改 Bar、D2D 或 Raw Input 状态。Draw2 在每个真实笔迹线程入口建立 RAII guard，所有正常、提前返回和异常退出路径都由析构发送 `Ended`。

#### 3. Contracts

- `Dormant`：第三光源目标透明度为 0，鼠标 Raw Input 必须注销；仅 UI3 窗口自然收到真实 `WM_MOUSEMOVE` 才能进入 `Inside`。
- `Inside`：注册鼠标 `RIDEV_INPUTSINK`；首次离开实际接收消息的窗口区域时进入 `Grace`，并记录 `GetTickCount64() + 5000` 的绝对截止时间。
- `Grace`：区域外移动不得重置截止时间。第三光源使用独立的 `240 × barStyle.zoom` 径向渐变；已发布 UI 外框只用于判断光圈是否可能命中 UI，从而裁剪渲染唤醒，不得作为全局亮度乘数。
- 同一控件的同一边框像素上，第三光源贡献只由光标到该像素的距离、生命周期强度和控件固定比例决定；光标是否位于接受消息区域、主栏或其他可见区域内不得改变该贡献。第一光源可独立影响最终合成结果，不属于该一致性契约。
- `Grace → Inside`：重新进入实际接收消息区域时取消定时器；仅回到 240px 邻域不能从 `Dormant` 唤醒。
- `Grace → Dormant`：绝对截止时间到达，或画布开始真实绘制时，注销 Raw Input 并从当前强度平滑淡出。落笔时若光标仍在 UI3 接收区，区域内后续移动不得重新激活；必须先收到离开，再由下一次自然进入激活。
- 5 秒等待使用窗口定时器，不得新增轮询线程或靠持续渲染计时。
- 并发笔迹由原子 activity count 合并：`0 → 1` 才向 Bar 窗口线程发送 Started，`1 → 0` 才发送 Ended。Bar 在绘图期间关闭边缘光影、停止第三光 Raw Input，并让非必要状态动画立即完成；最后笔迹结束后延迟 150ms 退出静默，避免短间隔连续落笔抖动。
- 绘图静默不允许画布线程直接写 UI 对象；按钮点击或必要状态变化仍可请求单帧反馈。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `RegisterRawInputDevices` 注册失败 | 第三光源保持隐藏；错误至多记录一次，不影响主光和 UI 交互 |
| `RIDEV_REMOVE` 注销失败 | 逻辑状态仍进入 `Dormant`，迟到的 `WM_INPUT` 必须被忽略 |
| 窗口定时器创建失败 | 立即进入 `Dormant`，不得无限保留全局跟踪 |
| 动画关闭 | 立即隐藏第三光源并请求休眠 |
| 触摸模拟鼠标消息 | 不得激活第三光源；画布休眠仅由 Draw2 统一落笔派发边界通知，不由模拟鼠标消息重复通知 |
| 笔迹在取得 Canvas 前提前返回 | RAII guard 仍必须发送 Ended，activity count 最终回到 0 |
| 多指笔迹交错结束 | 仅最后一个笔迹启动 150ms 退出定时器；任一新 Started 取消该定时器 |
| Started/Ended 窗口消息迟到或交错 | 窗口线程以当前原子 count 复核；过期消息不得错误进入或退出静默 |

#### 5. Good / Base / Bad Cases

- Good：离开 UI 后在外部持续移动，5 秒截止时间保持不变；第三光源按 240px × zoom 径向渐变连续归零，光圈离开全部可见外框后不再因位置变化唤醒渲染。
- Base：宽限期内返回 UI，取消休眠并从当前透明度继续淡入。
- Bad：把光标到主栏、绘制属性栏等任意区域的最近距离乘到所有控件的第三光源强度上；这会让其他区域为当前边框“托底”，造成等距离位置亮度不同。

#### 6. Tests Required

- 完整构建 `InkeysRepo.sln` 的 `Debug | ARM64`。
- 手工验证 UI 外启动、进入 UI、离开后 5 秒内返回、超过 240px、5 秒超时、休眠后仅靠近 240px、Draw2 鼠标/笔/触摸落笔、落笔时仍位于接收区和动画关闭。
- 对同一边框像素分别从接受消息区域内外取等距离光标位置，隔离第一光源后确认第三光源贡献一致。
- 性能验证至少比较 `Dormant` 与持续全局移动时的 CPU；`Dormant` 中不得出现由第三光源导致的持续渲染唤醒。
- 多指和快速连续短笔迹下记录 activity count/quiet 状态，确认无永久静默、无中途恢复动态光，最后一笔后约 150ms 恢复。

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

~~~cpp
// Wrong：只通知开始，线程提前返回或多指结束后 Bar 永久静默。
NotifyCanvasDrawingStarted();
RunDetachedStroke();

// Correct：每个真实笔迹线程以 RAII 成对通知，原子计数只发布首尾边界。
CanvasDrawingActivityGuard guard;
RunStroke();
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

### UI3 按钮边框光影约定

按钮未选中时通常保持背景透明，但仍可能需要显示鼠标第三光源。此时不得复用按钮本体 `pct` 作为边框光影透明度；应使用 `BarUiShapeClass::frameLightPct` 独立控制光影，保持 `framePct = 0`，并配置：

~~~cpp
shape.frameRendering = BarUiFrameRenderingEnum::PointLight;
shape.framePrimaryLightEnabled = false;
shape.frameCursorLightIntensityScale = buttonIntensity;
~~~

按钮的 `buttonIntensity` 应由颜色边框的第三光源强度按产品要求折算；本项目当前按钮使用色块边框强度的一半。按下时同时缩放按钮和 PointLight 边框，并降低 `frameLightPct`；PointLight 的 Gaussian 漫反射继续由 `BarRenderingAttribute::pointLightDiffuseExtraWidth` 覆盖脏区。

错误做法是把 `frameLightPct` 绑定到按钮本体 `pct`，这会让未选中按钮的透明背景同时隐藏其光影；正确做法是让 `Shape()` 在本体 `pct == 0` 且 `frameLightPct > 0` 时仅绘制 PointLight 边框。

### UI3 按钮悬停与按压衔接约定

主栏按钮的 `5s` 表示指针仍停留时，悬停背景完成快速显现后自然淡出的时长；它不是鼠标移出的退出时长。鼠标移出继续使用 `BarButtonHoverExitDur` 快速退出，避免残留多个按钮背景或在布局更新中出现长时间闪烁。

复用主栏悬停状态机的独立控件必须同时遵守：

- `Showing` 完成后才进入 `5s` 的 `Fading`，同一次进入不得反复重启计时；
- 鼠标移出调用普通非立即 `StopHover`，不得为独立控件另传 `5s`；
- 鼠标按下先建立按压状态，再以 `preserveVisual=true` 结束悬停，使透明度和填充色从当前值连续过渡到按下态；禁止先 `SetDirect(0)` 再升到按下透明度；
- 隐藏、失效或选择状态不再允许悬停时仍可立即清零，不能误用按下的保留视觉路径；
- 属性栏批次同步不得覆盖标记为 `animateWhenDisabled` 的悬停时长。

手工验证至少覆盖：指针驻留至自然淡出、淡出中移出、显现中按下、淡出中按下、拖出取消，以及抬起后未移动指针时不重新触发悬停。

### UI3 动画批次加入与关键帧中点

`【直接确认；设计约定】` `BarUiTimelineClass::CanJoin(double maxProgress = 0.5)` 是主栏布局变化和绘制属性加入主栏批次的统一判断入口；无效参数同样回退到 `0.5`，边界判断为 `GetProgress() <= maxProgress`。

| 线性时间轴进度 | 契约 |
| --- | --- |
| `progress <= 0.5` | 新目标可以复用当前批次的剩余时长，并继续共享原截止时间。 |
| `progress > 0.5` | 新目标必须创建完整的新批次；旧的单中间关键帧不再继承。 |

该边界与现有唯一中间关键帧的 `0.5` 时刻一致。若重新把加入阈值设到中点之后，会产生“关键帧已经过去、但新目标仍沿用旧批次语义”的区间，可能表现为重复收拢或后半程被过度压缩。修改批次阈值时应搜索全部 `CanJoin()` 调用，并手工覆盖中点前、中点附近和中点后的目标变化。

## 设置窗口与 ImGui

`【直接确认】`：

- `Setting.Base.cppm::CreateDeviceD3D` 调用 `D3D11CreateDeviceAndSwapChain`，请求 feature level 11.0 并创建独立 hardware device/context、discard swap chain 和 RTV；
- `Setting.cpp` 调用 `ImGui_ImplWin32_Init`、`ImGui_ImplDX11_Init/NewFrame/RenderDrawData/Shutdown`；`WM_SIZE` 只排队宽高，渲染线程释放 RTV、`ResizeBuffers` 后重建，遮挡时用 `DXGI_PRESENT_TEST` 降低忙等；
- 设置图片使用 `ID3D11ShaderResourceView*` 作为 `ImTextureID`；EasyX 提供的 BGRA 字节上传为 `DXGI_FORMAT_B8G8R8A8_UNORM` immutable texture/SRV；
- `Inkeys/Inkeys.vcxproj` 编译 `additional/imgui/imgui_impl_win32.cpp` 与 `imgui_impl_dx11.cpp`，仓库不再随附 ImGui DX9 backend；
- DX11 backend 不在运行时调用 `D3DCompile`：`IDR_SHADERS2` 是 VS、`IDR_SHADERS1` 是 PS，两个预编译 CSO 由 `Inkeys.rc` 嵌入 EXE；Inkeys 定制段在 backend 中用成对注释精确标记。

`【合理推断】` 设置 UI 的局部改动应复用 `Setting.Widgets`、`Setting.Wrap` 及现有字体/纹理路径；普通设置功能不得顺带更换 device、swap-chain、shader 或 SRV 所有权。

### 设置窗口 DX11 可执行合同

1. **Scope / Trigger**：修改设置窗口渲染、图片上传、ImGui backend 或内嵌 shader 时适用；不适用于 Bar 的共享 WARP/D2D device。
2. **Signatures**：`CreateDeviceD3D(HWND) -> bool`；`ResizeSwapChain(UINT, UINT) -> bool`；`LoadTextureFromMemory(const unsigned char*, int, int, ID3D11ShaderResourceView**) -> bool`。
3. **Contracts**：`TextureSettingSign` 保存 SRV；`ImGui::Image` 接收其 `(ImTextureID)(intptr_t)`；输入字节为 BGRA；资源映射固定为 `IDR_SHADERS2=VS`、`IDR_SHADERS1=PS`。
4. **Validation / Error Matrix**：device/RTV/texture/SRV 创建失败返回 false；resize 失败保留待处理尺寸并延时重试；`DXGI_STATUS_OCCLUDED` 切换到 `DXGI_PRESENT_TEST`；CSO 缺失或类型映射错误使 backend device objects 创建失败。
5. **Good / Base / Bad**：Good 为 show→resize→hide→show 后图片颜色和资源均正常；Base 为无 resize 的正常呈现；Bad 为把 DX9 texture pointer、BGRA 原始字节或 PS/VS 资源 ID 直接套用到错误 DX11 契约。
6. **Tests Required**：完整 Solution `Debug|ARM64` 构建；`fxc /dumpbin` 核对 VS/PS profile 与 POSITION/COLOR/TEXCOORD；EXE import 不含 d3dcompiler；手工检查 show/hide/resize/遮挡、十张图片颜色与透明度。
7. **Wrong vs Correct**：错误是把 `LPDIRECT3DTEXTURE9` 或 `ID3D11Texture2D*` 直接交给 DX11 backend；正确是创建 `ID3D11ShaderResourceView*`，保持到 draw submission 结束后再释放。

## EasyX、GDI/GDI+ 与特定窗口

`【直接确认】`：

- `IdtDrawpad.cpp` 使用 HiEasyX/EasyX `IMAGE`、GDI+ 曲线及软件合成，`DrawpadDrawing` 组合基础画布与活动笔画后更新分层窗口；
- `IdtFloating.cpp` 使用 EasyX/GDI+ 实现传统悬浮栏；
- `IdtPlug-in.cpp::PptUI` 在 HiEasyX/EasyX 背景上使用 D2D DC target 绘制 PPT 控件；
- `IdtFreezeFrame.cpp`、`IdtMagnification.cpp` 各自保留传统窗口/图像链。

这些是并存的当前实现，不是简单的“新后端取代旧后端”。修改前必须先确认目标窗口，不得把 Bar 的 D2D device context、设置的 ImGui DX11 swap-chain/SRV 或画板的 `IMAGE` 生命周期规则互相套用。

## Win32、DPI 与坐标

`【直接确认】` 程序有多个窗口和消息/渲染线程；多处使用 layered、transparent、no-activate/topmost 语义。`IdtWindow.cpp` 还维护窗口可见性、style 和置顶状态。`IdtMain.cpp`、`IdtDisplayManagement.cpp` 初始化系统版本、DPI 和显示器状态。

`【合理推断】` 修改尺寸、命中或窗口显示行为时，应追踪目标窗口实际使用的逻辑/物理坐标、显示器原点、DPI 缩放、分层窗口脏区及 mouse/touch/RTS 坐标转换；不要仅在创建点修改 style 后假设维护线程不会覆盖它。

## 建议验证范围

以下是由当前多后端结构推导的建议，不代表仓库已有正式测试政策：

- 目标窗口创建、显示/隐藏、缩放、透明边缘和退出；
- Bar 的脏区、静止 CPU 与 D2D 失败路径；
- 设置窗口的 DX11 resize/遮挡、hide/show/stop 资源清理，以及 SRV 图片颜色/透明度；
- 画板的基础层、活动笔画、撤销/恢复和 PPT 换页合成；
- `IdtFloating` 与 Bar 两条配置分支分别验证，直到维护者确认正式主路径。
