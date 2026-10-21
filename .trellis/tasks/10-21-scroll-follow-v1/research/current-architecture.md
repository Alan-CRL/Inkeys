# Research: 当前架构与实施锚点

- Query: 对照用户方案核查 feature/move 实际接入点
- Scope: internal / external
- Date: 2026-10-21

## Findings
- Inkeys/IdtMagnification.cpp: BuildCurrentFilterList/Win32MagnifierOperations 使用完整角色表，PrepareHidden alpha=0 -> Filter -> Source -> Redraw -> Reveal，MagnifierThread 只处理Freeze协调，没有PrintWindow。初始化/反初始化在 PrepareMagnifierWindow/ShutdownMagnifierWindow。
- Inkeys/Inkeys/UI/Freeze/Freeze.MagnifierCoordinator.h: BuildFilterList 模板可复用；Coordinator 的revision/停用事务不得被捕获占用。
- Inkeys/Inkeys/Window/Window.cpp: Overlay owner thread 创建现有Magnifier/双Drawpad/Bar；SetClickThrough只设置WS_EX_TRANSPARENT；SetDrawpadSurfaceVisibility负责原有成对呈现。捕获窗口需另一个owner，不能把PrintWindow放overlay/UI/Draw3线程。
- Graphics/Surface.Core.hpp + Surface.cpp: DibSurfaceImpl为top-down 32bpp DIB，resize复用、pixels/dc、RAII可用。
- Draw3.WindowControl.cppm:30: kCanvasNavigationProductIntegrationEnabled=false 必须保留。
- Draw3.DrawingController.cpp:14395 TranslateViewport 复用 ApplyCanvasContentTranslation/SetViewport、metrics raster invalidation、DiscardHotPreimages、viewportRefreshPending/clear transient/full present。14918起预算恢复L2/Canvas composition tiles，16364起trusted snapshot按viewport和history签名更新/平移fallback。
- Draw3.InkHistoryGpu.cppm: raster key明确不含viewport，Canvas-local cache可复用。
- Draw3.StrokeGeometry.cpp:891等存储点为输入+canvasOffset，新Down在DrawingController:10600锁存canvas.Viewport。
- IdtState.cpp:515起 ReconcileDraw3PresentationState 根据bridge/runtime/output ready处理双表面；注释明确主DComp不保证仅靠样式穿透。不能绕过握手。
- Draw3.Host.cpp: RefreshPresentationInputGateLocked 与DrawingController sealPresentationContacts有已接受输入收尾/直到Up隔离的既有机制。
- Draw3.AutoSave.cppm:17只有Clear/Exit触发；DrawingController:1349起选Desktop active/parked来源，1401复制viewport，1715/12735物化恢复viewport。共享uink_draw3_import/export/capture均传递viewport，格式v10无需改。
- UI/Bar/Bar.Button 注册、动态投影；Other.Config 的Extension默认仅MoreBoundary/Setting，官方扩展允许列表也需纳入新ID，不改固定区布局。
- IdtMain.cpp:325 InitializeDebugConsole保留重定向；诊断Session唯一实例，配置决定是否显示console；新CLI复用，不额外session。
- vcpkg.json与已安装Arm64 status固定opencv4 4.10.0#3，静态core/imgproc/video存在，无calib3d；保留现有features dshow/msmf/intrinsics/thread。

## External references
- https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow ：同步阻塞且公开列表未列PW_RENDERFULLCONTENT；必须真实PoC。
- https://learn.microsoft.com/en-us/windows/win32/winauto/magapi/magapi-intro ：Mag control和WOW64限制。
- https://docs.opencv.org/4.10.0/dc/d6b/group__video__track.html
- https://docs.opencv.org/4.10.0/dd/d1a/group__imgproc__feature.html
- 用户CSDN文章 https://blog.csdn.net/alan16356/article/details/139011874 抓取不稳定，PrintWindow旧经验以用户需求与本次PoC为准，不能声称已重现。

## Caveats
主branch为feature/move（用户明确覆盖原dev要求）。初始工作区干净。Python PATH指向不能运行的WindowsApps别名，使用bundled C:/Users/alan-/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe。用vswhere定位当前VS，不硬编码版本；若git status子模块权限失败，git status --ignore-submodules=all只作主树范围检查，不能宣称子模块已验。GUI已授权但尚未执行。MSBuild前同invocation去除PATH重复键并禁node reuse。
