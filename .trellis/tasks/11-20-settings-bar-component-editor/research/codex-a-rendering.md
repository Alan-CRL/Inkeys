# 组件页主栏预览 · 只读调查

**建议抽取可独立实例化的主栏内容层，再用设置页同一 D3D11 设备上的离屏纹理交给 ImGui 合成。** 现有第二宿主测试已经验证主栏内容可以换绘制目标，但重放仍借用原主栏的按钮、动画、字号和光照。〔【推测】`Bar.RenderLoop.cpp:10498–10538`，`Bar.Host.Test.cpp:207–225`〕

## 【事实】现有渲染路径

- **设置窗口**借用 `RenderPipeline` 的 D3D11 device/context，自建双缓冲、窗口化 `R8G8B8A8_UNORM`、`DXGI_SWAP_EFFECT_DISCARD` swap chain。ImGui 使用 Win32 + DX11 backend，绘制到 RTV 后 `Present(1,0)`。〔`Setting.Base.cppm:96–117`，`Setting.cpp:1534–1537,2306–2311,5155–5184`〕
- **设置页内**未找到 UI3 Renderer 调用。`EraserDemoGpu` 用预计算帧和图集，经一次 D3D11 draw 写入同设备 RTV/SRV，再由 `AddImageRounded` 采样。`Map` 写常量，像素留在 GPU。擦除光标和静态图则从 CPU 像素上传 immutable SRV，光标缓存生成时先解除预乘。`HomeBrand` 写 ImGui 顶点，`Widgets` 封装 ImGui 控件。首页缓存另走 GPU `CopyResource` / `CopySubresourceRegion`，透明组也有独立 RTV/SRV。〔`Setting.EraserDemoGpu.cpp:229–281`，`Setting.Pages.cpp:1160`，`Setting.cpp:155–197`，`Setting.Base.cppm:166–212`，`Setting.HomeBrand.h:342–371`，`Setting.Widgets.cpp:20–54`，`Setting.HomeEntryCache.h:65–116`，`imgui_impl_dx11.cpp:242–285`〕
- **启动预览**复用 UI3 材质和超椭圆几何，自行绘入 BGRA8 预乘、GDI-compatible D2D 位图，经 `GetDC(COPY)` 和 `UpdateLayeredWindowIndirect` 提交到独立 layered HWND。`PixelAssembly` 初始化另有 `CopyFromBitmap` + CPU `Map`，用于生成粒子。〔`StartupPreview.cpp:32–33,224–231,323–353,522–541,755–768`，`StartupPreview.Effects.Drawing.h:61–90`〕

## 【事实】主栏布局、状态和动画

- `Bar.Main` 的 `BarUISetClass` 持有按钮集、renderer、帧目标、光照、样式和面板栈。`Bar.RenderLoop` 算主栏目标位置与宽度，`Bar.Button` 把按钮映射到 `UI3.ListLayout` 装箱，`MainLayoutPolicy` 决定展开侧和横向镜像。`Bar.Layout` 提供绘制属性几何，`SurfaceLayout` 提供分页等横向表面几何。实际绘制由 `UI3.Panel` 画底板、`UI3.Button` 画按钮，底层使用 `UI3.Primitive` 和材质 renderer。〔`Bar.Main.cppm:865–932`，`Bar.RenderLoop.cpp:2902–2925,9744–9769`，`Bar.Button.cpp:2028–2068`，`UI3.ListLayout.cpp:97–153`，`Bar.MainLayoutPolicy.cppm:10–33,58–82`，`Bar.Layout.cppm:35–91`，`Bar.SurfaceLayout.cppm:62–109`，`UI3.Primitive.cppm:256,336`〕
- **实例边界**已有 `BarPageSurface` 和 `ShapeEditorSurface`，主栏没有同等的独立场景接口。隔离点包括 `barUISet`、全局协调器/`Client::Bar`、`BarAtomic` 唤醒标志、`floating_window` 提交、单个主内容输入 handler、全局 theme source 和共享光照发布。`Load()` 还会读写 `Inkeys::config`，绘制使用当前工具快照。Golden 测试重建全局对象，截获 ULW 源复制到 DIB；Host 测试用 `PlainBitmap` / `ExternalContext` 重放同一帧并回读，仅各自持有 renderer/目标。〔`Bar.Scene.cppm:115–138,201–218`，`Bar.Main.cpp:469`，`Bar.RenderLoop.cpp:2093,8292,8404,12993,13533–13539`，`Bar.Atomic.cppm:11–19`，`UI3.HostInput.cppm:102–104`，`Bar.Initialization.cpp:324–328`，`Bar.Button.cpp:1576–1596,1671–1683`，`Bar.Golden.Test.cpp:152–163,242–262,294–310`，`Bar.Host.Test.cpp:120–139,207–225,526–528`〕
- **动画**由 `UiValueClass::SetTar` 驱动。列表、图标顺序或宽度变化会更新布局批次，仍在列表且目标位移变化的兄弟按钮会随底板宽度补位。保留在列表中的隐藏按钮可退场渐隐，整表替换移出的项随即退出绘制遍历。`DisplayTransition` 处理跨屏位置、缩放和任务栏 Y 过渡。〔`UI3.Animation.cppm:627–631`，`Bar.RenderLoop.cpp:3645–3691,3720–3767,4034–4054,9755–9769`，`Bar.Button.cpp:1671–1674`，`Bar.DisplayTransition.cppm:24–80`，`Bar.RenderLoop.cpp:2296–2313,2558–2607`〕
- **光照、提示和 DPI**。阴影由材质、轮廓和倍率计算。主光锚在 Logo 或工具按钮并受工具/笔色影响，光标光以屏幕坐标减布局原点。Host 测试已经用预乘透明目标绘制，裁剪须留阴影外扩。小档提示由真实鼠标悬停触发，约 500 ms 后跟随光标，布局动画或拖动时关闭。大/小档高 80/42.5 DIP，实栏倍率取 DPI 或任务栏基准，再乘 `configZoom`。〔`UI3.Renderer.cpp:1777–1818`，`UI3.Material.cppm:147–155`，`Bar.Main.cpp:104–159`，`UI3.Lighting.cpp:199–200`，`Bar.Host.Test.cpp:337–360`，`Bar.RenderLoop.cpp:8087–8155,2601–2607`，`Bar.Metrics.cppm:97–124`，`Display.cpp:1372–1395`〕

## 【推测】接入方案

- **场景和输入**。从底板及按钮绘制块抽出可绑定任意目标的内容实例，注入独立按钮列表、根坐标、主题、尺度、动画时钟、局部主光与回调。预览根省去 Logo，固定顺序沿用镜像策略，其余采用 A1 → B1 → A2。按 `min(目标像素高 / 档位高, 可用像素宽 / 内容包络宽)` 居中适配。UI3 已有带接触 ID、捕获票据的 `HostInput`，以及点击、长按阈值、取消识别，拖放状态机需新增。实栏已有 Win32 捕获和 `WM_POINTER` / `WM_TOUCH`。设置 child 内把输入按实际 framebuffer 比例和预览原点映射到场景，按下绑定已呈现实例，跨 child 保留捕获，抬起或失焦统一结束，拖动时独占滚动手势。由通用 UI3 状态机发布位移、插入点和取消，Bar 策略决定分区、More 和垃圾区，拖动提示独立于悬停门禁。〔`Bar.RenderLoop.cpp:9744–9769,8087–8108`，`Bar.MainLayoutPolicy.cppm:20–33`，`UI3.Host.cppm:195–203`，`UI3.HostInput.cppm:16–55`，`UI3.Interaction.cppm:187–220`，`Bar.Interaction.cpp:3092–3110,3212,3380–3402`，`imgui_impl_win32.cpp:602–609,692–722`，`Setting.cpp:5098–5099`〕
- **方案 A，推荐**。同设备创建 BGRA8 `RTV|SRV`，以 `CreateBitmapFromDxgiSurface` 包装为预乘 D2D 目标，借 `FrameTarget::ExternalContext` 绘制，`EndDraw` 后供 ImGui 采样。使用 `ONE / INV_SRC_ALPHA` 合成并恢复状态，避免默认 `SRC_ALPHA` 再乘一次。把纹理内容 revision 纳入去重键，按设备代次重建。拖动浮层可延伸到页面覆盖层。代价是 GPU 重绘与采样，无 CPU 像素往返。〔`Draw3.RendererPattern.cpp:639–663`，`UI3.Host.cppm:22–29,54–69`，`imgui_impl_dx11.cpp:283–285,982–983`，`Setting.cpp:5127–5141`〕
- **方案 B，兼容回退**。同一场景画 `PlainBitmap`，`EndDraw` 后用 staging `CopyFromBitmap` / `Map`，解除预乘再上传 SRV。可沿用 ImGui 默认混合，代价是同步回读、CPU 转换与重传。两案均可沿用旧 swap chain，无 DirectComposition 或跨设备共享要求。现有管线请求 FL 11.1/11.0，Win7 旧运行时拒绝列表时重试 11.0，D2D 1.1 仍要求 Win7 SP1 Platform Update。〔`Bar.Host.Test.cpp:120–139,295–315`，`Setting.cpp:174–196`，`Setting.Base.cppm:102–113`，`RenderPipeline.cpp:194–241`〕 [Microsoft Platform Update](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/platform-update-for-windows-7)

## 【待确认】运行和页面接线

- 本次仅静态核验。需实测 Win7 SP1 + Platform Update 的硬件/WARP 互操作、浅深页面半透明边缘、设备重建，以及触摸与兼容鼠标去重、子区外释放。快捷卡可跳 `WindowLayout`，该页已有尺寸和图标顺序。当前分支 `tabPerformance` 未见二次确认重置，交互需按需求新建。〔`RenderPipeline.cpp:194–241`，`Setting.cpp:3581–3592,4644–4691`，`Setting.Pages.cpp:1495–1500,1597–1611`，`imgui_impl_win32.cpp:602–609,692–722`〕

**文件索引**　`Inkeys/Inkeys/UI/` 下按 `Setting/`、`StartupPreview/`、`Bar/`、`UI3/`、`RenderPipeline/` 查同名前缀。`Display.cpp` 在 `Display/`，`Draw3.RendererPattern.cpp` 在 `Drawing/Draw3/`，`Other.Config.cppm` 在 `Other/`。ImGui backend 在 `Inkeys/additional/imgui/`。

## 产品待选

- 【待确认】**生效时机**。拖放成功即保存并更新实栏，或先编辑再统一应用。建议前者，沿用现有组件页的即时同步语义。〔`Setting.cpp:4281–4290`〕
- 【待确认】**旧组件迁移与收纳**。保留旧投影的首两项在栏、其余在 More，或全部已启用项先入栏。建议保留现状迁移一次，随后用组件类型与实例 ID 分开表达，允许同类型多份和整类删除。现有 `ExtensionButtons` 仍未接入实栏。〔`Bar.Button.cpp:1505–1559,1576–1580,1622–1648`，`Other.Config.cppm:256–261`〕
- 【待确认】**连续分隔线**。每条占独立窄槽，或多条合为一个可选分隔组。建议独立窄槽，让每条可拖可删，固定边界线继续用原装箱语义。小档现有零宽边界连续放置会共用 X。〔`Bar.Button.cpp:1562–1573,2082–2087`，`UI3.ListLayout.cpp:107–123`〕