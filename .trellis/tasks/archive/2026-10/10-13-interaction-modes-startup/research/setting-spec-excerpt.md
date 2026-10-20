# Setting spec 摘录

来源：.trellis/spec/native-desktop/rendering-and-ui.md；完整规范为权威。

## 设置窗口与 ImGui

`【直接确认】`：

- `Setting.Base.cppm::CreateDeviceD3D` 借用 `FrameContext::epoch` 的共享 WARP device/immediate context，并通过传统 `IDXGIFactory::CreateSwapChain` 创建 Setting 独占的 discard swap chain 和 RTV；
- `Setting.cpp` 的持久协程 session 调用 `ImGui_ImplWin32_Init`、`ImGui_ImplDX11_Init/NewFrame/RenderDrawData/Shutdown`；`WM_SIZE` 只排队宽高，渲染线程释放 RTV、`ResizeBuffers` 后重建，遮挡时用 `DXGI_PRESENT_TEST`；
- 设置图片使用 `ID3D11ShaderResourceView*` 作为 `ImTextureID`；`DibSurface` 的 BGRA 字节上传为 `DXGI_FORMAT_B8G8R8A8_UNORM` immutable texture/SRV；
- `Inkeys/Inkeys.vcxproj` 编译 `additional/imgui/imgui_impl_win32.cpp` 与 `imgui_impl_dx11.cpp`，仓库不再随附 ImGui DX9 backend；
- DX11 backend 不在运行时调用 `D3DCompile`：`IDR_SHADERS2` 是 VS、`IDR_SHADERS1` 是 PS，两个预编译 CSO 位于 `UI/RenderPipeline/Assets/ImGui` 并由 `Inkeys.rc` 嵌入 EXE。

`【合理推断】` 设置 UI 的局部改动应复用 `Setting.Widgets`、`Setting.Wrap` 及现有字体/纹理路径；普通设置功能不得顺带更换 device、swap-chain、shader 或 SRV 所有权。

### Setting 外观条件卡片约定

- “动画速率”只在 `Experimental.Inkeys3.UI3.Animation.Enable=true` 时显示；“动态边缘光影”只在 `Experimental.Inkeys3.UI3.EdgeLighting.Enable=true` 时显示。隐藏只影响布局，不得覆盖 `SpeedRate` 或 `EdgeLighting.Dynamic` 的持久化值。
- 外观容器高度和两个条件卡片的渲染门禁必须读取同一份帧开始快照，按 `4 + showAnimationRate + showDynamicEdgeLighting` 张卡片计算；不得分别读取切换前后的 bool，否则同一帧会出现空白或裁剪。
- 用户可见标题走 i18n；改标题时保持不可见 ImGui ID 稳定，避免无必要地重置控件身份。

### Setting 统一渲染与业务队列合同

#### 1. Scope / Trigger

修改 Setting 显隐、WndProc、ImGui session、swap chain/RTV/SRV、配置持久化、模态动作或退出顺序时适用。

#### 2. Signatures

~~~cpp
bool Setting::Initialize();
void Setting::Shutdown() noexcept;
void Setting::Show();
void Setting::Hide();
void Setting::Toggle();
bool Setting::IsVisible() noexcept;
WNDPROC Setting::WindowProc() noexcept;
FrameResult RenderSettingFrame(const FrameContext&);
~~~

#### 3. Contracts

- HWND 所属线程只处理 capture/cursor/IME、生命周期和 `ImGui_ImplWin32_WndProcHandlerEx`；专用 recursive mutex 串行访问 ImGui IO，因为 Win32 backend 的 `SetCapture`/`ReleaseCapture`/IME 调用可能同步重入同一 WndProc。ImGui context、DX11 backend、NewFrame/draw/Present 只由 RenderPipeline 线程使用。
- Setting 可见且可呈现时返回 `Continue`，因此独自按统一 16,666,667 ns 上限连续绘制；隐藏时逆序释放 backend、SRV、swap chain/RTV，返回 `Idle`。resize、遮挡和 epoch 变化由 `SessionState` 决策，不在 WndProc 直接操作 D3D。
- 文件写盘、Shell、模态确认、重启和 DDB 操作进入单一 FIFO。配置命令在生产者线程冻结 JSON 或 `Inkeys::Config` 副本；配置 I/O 命令的 worker 不读取实时 `setlist`、`pptComSetlist` 或 `Inkeys::config`；绘图状态同步命令按下文专用合同读取最新运行时状态。停止时禁止新命令，并按 FIFO 排空已接收命令。
- 自动更新是既有长期网络服务；FIFO 只串行化其启动命令，不把长期下载循环占用为业务 worker 本体。
- 退出顺序固定为：停止显隐/输入生产者，隐藏并请求 Settings，渲染线程 drain session，`Unregister(Settings)`，排空业务 FIFO，join Bar/PPT，停止 Window Service，最后 `RenderPipeline::Shutdown()`。

#### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `Present` / `ResizeBuffers` 返回 device removed/reset/internal error | 返回 `DeviceLost`，由唯一管线线程恢复 epoch |
| 普通 Present/resize 失败 | 只返回 `Retry` 并重建 Setting session，不影响 Bar/PPT |
| `DXGI_STATUS_OCCLUDED` | 后续帧用 `DXGI_PRESENT_TEST` 探测，成功后恢复绘制 |
| 隐藏或 generation 变化 | 在渲染线程逆序释放旧 session；可见时按新 epoch 重建 |
| 业务命令完成 | 发布不可变 completion snapshot 并只请求 Settings |
| 退出时 FIFO 尚有配置写盘 | 排空后 join，不得 request_stop 后清空未执行命令 |
| Bar 在 Setting drain 前观察到 `offSignal` | Bar 返回 `Idle`，不得以 `Stop` 提前结束共享线程 |

#### 5. Good / Base / Bad Cases

- Good：Setting 连续绘制时 Bar/PPT 保持 idle；隐藏后 session 释放且管线无限等待，最后一次配置仍写盘。
- Base：show→resize→occluded→visible→hide→show，epoch 不变时复用共享 device 并重建 Setting 私有呈现资源。
- Bad：在渲染回调直接 `Write()`/`MessageBox`，或 worker 到退出时清空尚未执行的 FIFO；前者阻塞所有 UI，后者丢配置。

#### 6. Tests Required

- Headless 覆盖显隐、resize、occlusion、generation、业务完成快照和 device loss 分类；WARP 初始化断言 FL11.0+、context、DXGI/D2D/DWrite 资产有效。
- 完整 Solution `Debug|ARM64` 构建，静态审计旧 hardware device、24 FPS、`SettingMain`、`test.select`、运行时 `D3DCompile` 和 flip/DirectComposition 均不存在于活动路径。
- GUI 受限任务只运行 `InkeysHeadlessTests.exe --no-window`。Win7 SP1 + KB2670838 只可声明传统 CreateSwapChain/discard/FL11.0 fallback 的静态兼容，未经实机不得声称已验证。

- `Inkeys.UI.Freeze` 把业务 flags 与单调 `revision` 放在同一原子状态字中，每次真实 Toggle 或 PPT/Whiteboard 工作区切换发布完整 `StateSnapshot`。observer 只能把 `{revision, active}` 投递到 latest-wins mailbox 并唤醒 `MagnifierThread`，不得直接操作 HWND；迟到通知按 revision 丢弃，失败回退只能调用 `DeactivateIfRevision(request.revision)`，禁止无条件 `Toggle()`。
- `MagnifierThread` 是桌面定格唯一的 Magnifier 更新/显示协调入口。一次有效 active revision 依次校验 Window Service 当前 Host/Child、保持 Host 透明并显示 Host/Child、从当前角色 HWND 重建并提交排除列表、提交一次 source、invalidate/redraw，最后再校验 revision 与当前句柄后揭开 Host；Reveal 后仍需复查，过期结果必须立即 conceal 且不得提交 applied/visible。过滤、source 或必要显示步骤任一失败都停止成功链、条件撤销对应 Freeze revision，并允许后续新 revision 重试。inactive、工作区切换、停止和退出只透明并隐藏 Magnifier Host/Child，不隐藏 Bar/Drawpad，不改变 owner/topmost/focus/taskbar。`FreezeFrameWindow` 只保留 Freeze/PPT/Recall surface 绘制，不得调用 Magnifier 更新、显隐或 alpha API。
- 桌面定格不得调用 `SetOverlayFullscreen`：Magnifier 和 Freeze surface 都继续使用显示器高度减 1 像素，Shell fullscreen 标记只属于白板工作区。Window Service 重建 Magnifier 生命周期时先停止旧 coordinator，再在新 `PrepareMagnifierWindow` 生命周期 reset；每次事务重新读取当前 Host/Child 和完整排除集合，不能缓存旧 HWND。
- Setting 创建时及 `IdtSelection` 选择态的 owner 必须为 null；所有非选择态通过 `SetSettingOwnedByDrawpad(true)` 把 Drawpad 设为 owner，回到选择态通过 `SetSettingOwnedByDrawpad(false)` 清除 owner 并以 `HWND_NOTOPMOST` 退出画布置顶链。owner 修改必须投递到 Setting 所属线程，重复请求幂等；目标 HWND 缺失、Win32 调用失败或最终 `GW_OWNER` 不符合请求时返回 false，部分修改必须回滚原 owner。`SyncDraw3State()` 保存带版本的最新期望值，提交失败后由 `StateMonitoring()` 的 250ms 节拍重试；提交与 applied 写回必须串行，并在持锁后丢弃过期版本，避免旧命令覆盖新模式或产生假收敛。该动态 owner 是 Setting 唯一受控例外，不得直接从工具按钮、快捷键或渲染线程修改。
- Setting 始终是可激活的顶层 owned/unowned popup，而不是真正的子窗口：style 固定为 `WS_POPUP | WS_CLIPCHILDREN`，不得包含 caption/thickframe/minimize/maximize/system-menu；ex-style 包含 `WS_EX_APPWINDOW` 且排除独立 topmost/layered/noactivate/toolwindow。owner 切换不得改变 style、ex-style、图标、窗口线程或任务栏按钮；显示时由所属窗口线程主动 restore/show 并请求 foreground/active/focus；`WM_GETMINMAXINFO` 把最小/最大 track size 固定为配置尺寸。
- Setting 自绘标题栏的可拖动空白区必须在 ImGui 消息转发前处理 `WM_NCHITTEST` 并返回 `HTCAPTION`，由 Win32 非客户区移动循环负责拖窗；当前几何为缩放后的 `0 <= y < 32 DIP` 且 `0 <= x < 914 DIP`。右侧 `914..960 DIP` 关闭按钮和正文返回 `HTCLIENT`，继续由 ImGui 处理隐藏与内容交互。该命中合同不得因 Setting 是否拥有 Drawpad owner 而分支，`WM_MOVE` 继续记录最终位置。
- 主栏设置按钮调用统一 `UI::Setting::Toggle()`：隐藏时显示并激活；已显示但真实 Setting HWND 不是前台线程的焦点窗口时，只重新提交 Setting `Show` 命令以 restore/foreground/active/focus；仅在已显示且前台窗口与真实焦点 HWND 都是 Setting 时隐藏。Bar 保持 `WS_EX_NOACTIVATE`，只读取焦点状态，不得从 Bar 输入线程直接调用 `ShowWindow`、`SetForegroundWindow`、`SetActiveWindow` 或 `SetFocus`。
- `DibSurface` 是 top-down 32-bit BGRA DIB Section。HDC、HBITMAP、旧选入对象和像素地址由 RAII 管理；复制为深拷贝，移动为 `noexcept`，resize 先成功创建新资源再交换。
- HiMsg 成功 `Get/TryGet` 即消费；合成输入通过 `Enqueue` 原样进入同一队列。触摸转单指的 mouse message、坐标、按键状态和 marker 字段不得丢失或重新解释。
- HiMsg 默认接受 Win32 系统生成的触摸兼容 mouse；这是公共库行为。只有已经自行处理 `WM_TOUCH` 并合成单指输入的 Inkeys Bar/PPT binding 才设置 `WindowSpec::messageCallback`，在 HiMsg subclass 自动入队前对 `IsTouchGeneratedMouseMessage(message, GetMessageExtraInfo())` 返回 `Action::Discard`。该 callback 仍继续原 WndProc；真实鼠标和不带 touch flag 的笔兼容 mouse 必须保留。
- 所有使用 HWND 的渲染/交互 `jthread` 必须先 stop/join，最后才调用 Window Service `StopAndJoin()` 逆序销毁窗口。

### 4. Validation & Error Matrix

| 条件 | 必须行为 |
| --- | --- |
| `beforeCreate`、注册类、CreateWindow、HiMsg bind 或 `created` 失败 | 回滚 HWND、channel、class、thread id 和已激活 lifecycle；optional role 不拖垮同组 |
| 动态重建窗口 | 当前 `activeSpec` 决定 cleanup；不得调用旧 spec 的 `destroyed` |
| Mag 创建失败 | 跳过 Mag child，Freeze 成为 overlay root |
| 静态或动态创建双画布 | `GW_OWNER(DrawpadPresentation)=Freeze`，`GW_OWNER(Drawpad)=DrawpadPresentation`；两者仍为顶层 popup |
| Whiteboard window mode 切换失败 | 回滚已修改成员的 style/visibility，不发布稳定 workspace 状态 |
| Whiteboard group 收到最小化/恢复 | 保存成员可见性；恢复时只显示此前可见成员，不激活辅助窗 |
| Setting 创建规格传入 overlay ex-style 或 owner | Service 强制归一化为普通 app window 且初始 owner=null；运行时 owner 只能由 `SetSettingOwnedByDrawpad` 修改 |
| `SetSettingOwnedByDrawpad(true)` 缺少 Setting 或 Drawpad HWND | 返回 false，不改变现有 owner/style/Z 序 |
| Setting owner 写入失败或最终 `GW_OWNER` 不匹配 | 回滚调用前 owner 并沿用 Window Service 命令失败日志 |
| Setting owner 同步提交失败 | 保留 desired/applied 版本差异，按 250ms 状态节拍重试直至最新模式生效；失败日志由 Window Service 去重 |
| owner 提交等待期间发布了新模式 | 旧请求持锁后若已过期则不提交；若调用期间才过期，记录实际应用的旧版本，使下一轮继续收敛到新版本 |
| `SetSettingOwnedByDrawpad(false)` 成功 | owner=null，执行 `HWND_NOTOPMOST`；窗口仍保留 `WS_EX_APPWINDOW`、激活能力与图标 |
| Setting `WM_NCHITTEST` 位于缩放后的标题栏空白区 | 在 ImGui 前返回 `HTCAPTION`，进入系统 move loop |
| Setting `WM_NCHITTEST` 位于关闭按钮或正文 | 返回 `HTCLIENT`，不得截断既有 ImGui 点击/隐藏路径 |
| 主栏设置按钮：Setting 隐藏 | 提交 `Show(Setting)`，显示并激活 |
| 主栏设置按钮：Setting 可见但未拥有前台焦点 | 再次提交 `Show(Setting)` 恢复并聚焦，不隐藏 |
| 主栏设置按钮：Setting 可见且拥有前台焦点 | 提交 Hide，关闭设置窗口 |
| Bar/PPT 收到系统触摸兼容 mouse | HiMsg callback 不入队但继续 WndProc；业务 WndProc 同样返回 0，自定义 `WM_TOUCH -> Enqueue` 是唯一单指来源 |
| PPT hide 后重新 show 或交互前置 | owner 仍为 Drawpad，目标位于 Bar 正下方，且前台/焦点窗口不变化 |
| PPT 进入放映时根刷新失败 | 保留一次 refresh pending；后续状态发布继续请求根刷新，成功或离开放映后清除 |

## Setting 绘制期间的 Draw3 同步合同

### 1. Scope / Trigger
设置会话在 ImGui 临界区内直接或经橡皮等业务 setter 调用 SyncDraw3State 时适用。2026-10-13 卡死源于渲染线程持 ImGui 锁等待 Setting HWND 命令，而 HWND WndProc 正等待同一把锁。

### 2. Signatures
- `Setting::DeferDraw3SyncIfRendering() -> bool`。
- `SetListStruct.paintDevice: IdtAtomic<int>`，`CaptureSettingJson`显式load并保持PaintDevice整数；每次Sync只load一次用于比较和赋值。
- `SettingRenderScope` 标记当前线程正在设置会话 Resume；`settingRenderingOnCurrentThread` 为 thread_local。
- `SyncDraw3State()` 入口先调用 defer guard；业务 FIFO 增加 `SettingBusinessKind::SyncDraw3State`，worker 调用全局生产 Sync。

### 3. Contracts
- 设置绘制作用域内 guard 返回 true 并转交现有 FIFO，调用方立即返回；不能持 ImGui 锁同步等 HWND。
- 非设置绘制线程 guard 返回 false，保持原有同步语义。thread_local 防止 worker 再次延后自己。
- 选项先更新当前配置，Sync 命令随后按最新状态应用；配置写盘仍在生产者线程冻结 payload，并排在同一 FIFO 中的 Sync 之后。
- Setting owner 的 HWND 线程所有权、desired/applied 版本与 250ms 重试保持不变，不用 try_lock 丢弃同步或跨线程直接改 HWND。
- 停止生产后排空已接受命令；停止期拒绝入队沿用现有队列语义，不能回退到锁内执行 Sync。guard 的 true 表示已拦截同步，不等于窗口事务已经应用成功。

### 4. Validation & Error Matrix
| 条件 | 行为 |
| --- | --- |
| Render 持 ImGui 锁，模拟 HWND 正等锁 | 同步提交立即返回；释放 render 锁后 worker 能完成 |
| 非 render 调用 | 原同步路径执行 |
| 快速连续修改 | FIFO 保持顺序，应用最新运行时状态，写盘消费各次已冻结 payload |
| 停止队列 | 已接受命令排空，新命令按既有停止语义拒绝，不在 render 锁内执行 |
| 窗口 owner 应用失败 | 保留既有 desired/applied 差异及重试 |

### 5. Good / Base / Bad Cases
- Good：模式下拉或橡皮 setter 发起 Sync 后渲染继续；worker 执行实际 HWND 调度。
- Base：状态监控、非设置工具入口继续同步应用。
- Bad：只换 Combo/Radio 控件，仍在 ImGui 锁内等待 Window::Submit 的 future；或只用计数式 Sync 桩测试就宣称卡死已修复。

### 6. Tests Required
- 使用生产业务 FIFO、render 标记及 defer guard 的无窗口线程回归：锁内提交不阻塞、worker 竞争锁后完成、非render语义、FIFO顺序、重复修改/停止排空。
- 控件回放另验证三语下拉与信息卡片缩放几何；它不能代替线程调度测试。
- 完整 ARM64 solution 与 Headless --no-window；受限环境出现已知 PDB/文件权限失败时区分环境并按权限规则复核。

### 7. Wrong vs Correct
```cpp
// Wrong：设置会话持 ImGui 锁时进入同步 HWND 等待。
void SyncDraw3State() { ApplySettingOwnerDesiredState(...); }
// Correct：只延后设置渲染发起的同步，worker 和其他线程仍走原生产路径。
void SyncDraw3State() {
    if (Inkeys::UI::Setting::DeferDraw3SyncIfRendering()) return;
    // 原有状态/owner/呈现同步。
}
```
