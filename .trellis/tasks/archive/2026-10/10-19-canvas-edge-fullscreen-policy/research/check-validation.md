# 双画布策略审阅与验证（2026-10-19）

## 审阅范围与上下文

本轮为 canvas 子任务的提前审阅，责任仅限 Window.cpp、Window.CanvasPolicy.h、window_tests.cpp 和 animation_tests.cpp 的无窗口调用入口。Display/Bar/Startup/IdtMain 等仍由其他实施者处理，未覆盖其改动。已读子任务 PRD/design/implement/check.jsonl、父 research/canvas-scene-policy.md 与实现记录；显式补读 native-desktop/draw3-integration.md 的窗口所有权/样式透明度、rendering-and-ui.md 的 Whiteboard 与窗口服务合同、native/runtime-and-rendering.md 的 resize 与绘制线程边界，以及 build/native/PPT 相关 manifest 文档。

## 已修复发现

1. **旧可见意图覆盖较新隐藏请求**：EnsureCanvasPolicy 在几何或回滚故障后保存 recoveryVisible；原先 Hide、HideAll、SetDrawpadSurfaceVisibility(Hidden) 没有更新该快照，250ms owner 重试成功会把已明确隐藏的旧表面重新显示。现在三个生产路径使用内部 NoteCanvasRecoveryHidden 清除恢复显示意图，保留 pending ack 状态；无窗口回归走实际 helper 和实际双画布事务控制流程，验证故障后 Hidden 请求不会在成功恢复时揭开旧表面。
2. **几何查询失败提前返回没有关闭覆盖**：GetWindowRect/GetMonitorInfo/窗口记录无效时，Ensure 原来在进入事务前直接返回；真实 bounds 已变化时旧 region 可能已经不能排除屏幕底行。现在在 owner 线程先捕获双窗现有显隐，再共同隐藏、失效 applied，并保留250ms失败重试。先保存两窗再隐藏，避免 owner 链副作用污染另一窗的原显隐。
3. **重复故障可能重显旧 region**：首次几何失败会清除 applied，下一次不能仅靠 applied key 发现旧 region 的几何来源。现在 recoveryVisible 有值也视为未收敛的安全隐藏，第二次失败即便 region 回滚成功仍不恢复可见性，实际策略成功后才清除恢复记录。此发现由整合审阅代理交叉指出并由本代理核实修复。
4. **资源断言冷启动误报**：独立执行实际 RunCanvasPolicyTests 后，原来在冷启动前后直接比较进程 GDI 对象数的新增断言失败；重复构建/执行可复现，GdiFlush 也不消除固定初始化/缓存差异。现在用两个完全相同的64次生产 region 构造/复制/裁剪/恢复批次，第一批完整预热、第二批要求总数完全相等；没有允许按调用次数增长，也没有删除资源释放检查。修复后测试通过。

## 生产路径核对

- 两 canvas 创建规格在 CreateWindowEx 前清除 WS_VISIBLE，owner 在首次 Show 前保存本代原 region/NonRude 并应用策略；重建和 ResetState/Cleanup 均重置本 HWND 状态。
- 双窗明确参加 region/property 事务，不依赖旧 RequiresOverlayActivationStyleTransition 对辅助窗的筛选；两窗先共同隐藏，事务应用及既有 style 成功后才按原互斥显隐恢复并提交 whiteboardWindowMode。
- HRGN 成功 SetWindowRgn 后移交系统，快照与回滚使用独立复制，失败由 RAII 删除；白板恢复创建时原 region/属性，当前产品没有其他 region 写入者。
- 同 scene/HWND/bounds/monitor key 不重复 region/属性/隐藏显示；Desktop 与 PPT 共用非白板策略，不读取任务栏/DPI/UI倍率。
- SetBounds(Drawpad) 同步两个 HWND，并在批量定位部分失败后仍按实际 RECT 重新求策略；Show、非 Hidden surface 交接和白板恢复前核对策略。
- canvasPolicyApplying_ 阻止同步 USER32 回调重入提交；owner 消息循环仅失败时启用250ms重试，正常使用无限等待，shutdown 不再启动恢复显示。
- 未改变 Window 公共接口、Draw3 Host/RTS/presenter/document/撤销/持久化或资源 resize 路径。Region 不改 HWND/client RECT 和坐标原点；相关模块源码未修改，所以不新增实际 Host resize 的调用，但本轮没有 Host 运行期计数证据。
- Freeze/root 既有 MarkFullscreenWindow best-effort 机制保留；NonRude 设置对象只有两画布，未把期望值称作已验证的 Shell 实际分类。

## 实际验证

- `git diff --check --ignore-submodules=all -- Inkeys/Inkeys/Window/Window.cpp InkeysHeadlessTests/window_tests.cpp InkeysHeadlessTests/animation_tests.cpp`：退出0。
- 格式：Window.cpp、Window.CanvasPolicy.h、window_tests.cpp 无 BOM + CRLF；animation_tests.cpp 保留 BOM + CRLF；四文件均无 bare LF。
- 独立无窗口用例：从当前 `InkeysHeadlessTests/window_tests.cpp` 原样提取实际 `RunCanvasPolicyTests()`，直接 include 生产 `Window.CanvasPolicy.h`，只增加 console main；不复制或另写测试逻辑。临时来源与脚本位于忽略的 `.trellis/.runtime/canvas-check/`。
- 当前 VS 经 vswhere 定位，使用 ARM64 原生 cl 14.51.36231、Windows SDK10.0.26100.0；参数 `/std:c++20 /permissive- /EHsc /W4 /WX /utf-8 /DUNICODE /D_UNICODE /MTd`，链接 user32/gdi32，构建退出0、无 warning。运行退出0，输出 `[CanvasPolicy] failures=0`。
- 覆盖生产区域几何/NonRude 值、负坐标、底行/倒数第二行、原自定义区域恢复、跨屏交集、key换代/几何失效、双窗提交与逆序回滚、每个forward故障步骤、回滚未收敛双隐藏、几何不安全回滚不揭开、较新Hidden请求和白板退出。
- animation_tests.cpp 无条件调用 RunCanvasPolicyTests，位于 `if (runWindowTests) RunWindowTests()` 之前，因此统一 `--no-window` 路径会执行。

## 未修复问题与验证边界

本轮范围内没有保留未修复的具体代码缺陷。Window.cpp 模块本体的完整类型/链接验证与全组 Headless --no-window 由父任务统一 Debug|ARM64 solution 构建完成；本代理不并行启动 MSBuild 或争用工程缓存。

无窗口测试的 Win32 transaction operations 为故障注入适配器，证明生产控制流程与区域/GDI几何，不证明真实 USER32 消息时序、DComp/ULW 可视区域、Shell 非全屏分类或 Explorer 自动隐藏升起；owner 几何查询失败和重复故障的调用接线另经源码逐路径核对。未启动应用或 GUI，未操作实际任务栏，未执行 commit/push。
