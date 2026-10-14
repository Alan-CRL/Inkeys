# 拖动释放第三光源延续验证

## 提交与任务状态

用户要求先提交既有改动，已创建35afa7ee5f7752f6ae91442624ba05c4f840b1b6（feat(toolbar): add icon order and expansion direction settings），提交后工作树干净。延续同一toolbar-order-expansion任务，不结束任务、不push。本次光源修复暂不再次commit。

## 原因与修复方向

源码确认：第三光源输入既保存屏幕物理点，也缓存按最后成功呈现原点/位移换算的局部点。释放拖动后，RenderLoop吸收位移、平移根几何并重基准，但PrepareFrameLighting仍读取旧局部缓存。无需新鼠标消息也应正确，现状却会等下一次输入才恢复。此坐标缺陷已确认，用户真实视觉闪烁的消息时序尚未GUI验证。

采用真实Prepare入口每帧以同帧稳定布局屏幕原点换算screenPoint。保留serial、radius/intensity/visibility、原生命周期和damage/逆映射、分页共享屏幕快照；不把普通stale presentation未证实边界并入修复范围。详细源码证据见research/drag-release-third-light.md。

## 自动结果

| 阶段 | 状态 | 证据 |
|---|---|---|
| 原实现真实Prepare回归 | 已确认：完整红测构建退出0；旧实现离屏退出1，恰好48条新增坐标/damage/连续帧断言失败，原有回归与PageControlScene无失败 | Build/ToolbarOrderExpansionValidation/drag-light/red-bar.stderr.log、red-offscreen-results.log |
| 修复后Trellis检查 | PASS，无源码/测试机械问题需自修 | research/drag-light-check.md |
| 最终Debug ARM64完整solution | PASS，退出0、0 errors，耗时2分44秒；既有warnings保留 | Build/ToolbarOrderExpansionValidation/drag-light/solution.log |
| 修复后真实Bar离屏 | PASS，退出0；EraserVisual/PageControlScene failures=0；24组连续帧、共享映射、门禁及4组像素通过 | Build/ToolbarOrderExpansionValidation/drag-light/bar-offscreen.stderr.log, offscreen-results.log |
| Headless --no-window | PASS，退出0；Config metadata 0 failures，纯策略/坐标/停靠/动画及全部既有套件通过 | Build/ToolbarOrderExpansionValidation/drag-light/headless.stdout.log |
| diff/原编码/BOM/CRLF | PASS，12个已有文件格式保持，git diff --check通过；新增研究/验证文件UTF-8 CRLF | 主会话检查 |

构建沿用动态原生ARM64 MSBuild、同invocation PATH规范化、既有命令范围串行/已装静态依赖/非增量链接参数，至少允许5分钟；不改工程、SDK或依赖。所有CLI为明确的初始化HWND前无窗口入口，Hidden启动并等待真实ExitCode。

## 修复前确定性证据

测试固定screenPoint和serial=41，4组坐标域×0.6/1/1.5/2缩放共16组，每组释放后连续120帧。核心例：屏幕光标(900,450)，拖动时origin=(300,0)，局部光源(600,450)正确；吸收后origin=(0,0)，旧实现仍保持局部(600,450)，稳定检查失败。其余负坐标、两轴、显示原点与吸收基准同样失败，每组仅3个预期FAIL，共48。已保存red-offscreen-results.log防止后续通过日志覆盖。

## 修复后结果

真实Prepare的6组原点×4缩放共24组，固定screenPoint/serial=41，释放后连续120帧全部stable=1。核心场景从旧local600→600（失败）变为600→900（正确）；原点不变时保持局部光源及damage稳定，实际残差、负坐标/跨原点、live/presented干扰和共享Surface映射均通过。原Inside/Grace/Dormant及未ready、光照/动画门禁保持。

实际D2D第三光像素对照（关闭第一光、基础边框及填充，调用真实Prepare与Shape绘制）：

| zoom | 非透明第三光贡献字节数 | 释放前后最大BGRA差值 | 关闭第三光后非透明贡献 |
|---|---:|---:|---:|
| 0.6 | 4972 | 0 | 0 |
| 1.0 | 13440 | 0 | 0 |
| 1.5 | 25372 | 0 | 0 |
| 2.0 | 50480 | 0 | 0 |

最终构建仍出现既有applocal找不到pwsh.exe的9009环境提示，MSBuild最终exit0；已安装静态依赖下新生成的Bar与Headless无窗口程序均正常运行。未改工程/SDK/依赖/全局PATH，环境提示与源码结果分别记录。设置/i18n代码没有变化，沿用初版已通过结果，不重复运行。

## 边界与人工验收

测试应直接执行生产Prepare，而非用手工最终光照快照替代。未运行整个私有RenderLoop、真实消息/ULW或HWND互动；研究/检查及完整编译验证同帧参数接线和既有呈现屏障。未启动GUI或Computer Use。人工待验：真实鼠标/触摸拖动释放、主栏展开/折叠、两侧方向/换边、浮动/停靠、DPI/多屏和连续拖动的第三光源视觉连续性。

## 最终状态

产品修复3文件（Rendering.cpp/.cppm、RenderLoop.cpp）与真实Bar回归1文件，未修改Input/Raw Input/timer/分页发布/呈现策略或配置/i18n。完整构建、先失败后通过与24组稳定/4组像素/既有Headless均有证据。Trellis任务保持in_progress；先提交的35afa7ee保留，本次修复未再次commit，未push/finish/archive。

## 最新提交授权

用户随后明确要求为本次光源修复提交commit，但不结束任务。本次提交包含已经通过上述验证的修复、回归测试和Trellis记录；此前“暂不再次commit/修复未提交”的文字描述授权前阶段。任务继续in_progress，不push/finish/archive。提交哈希以Git历史为准。
