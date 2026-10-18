# F-019 Scene 设备位图缓存独立复审（2026-09-28）

范围：只读复核本轮 `Bar.Scene.cpp` 的 F-019 差异、`Bar.EraserAttribute.Test.cpp:729-838` 的 Scene 回归，并追踪 `Bar.Rendering.cpp`、`Bar.UI.cpp`、PageControl、Whiteboard、RenderPipeline 的实际调用链。未执行构建、性能采样或 GUI。当前工作区还有其他并行修改；本审查不把测试文件中 exact-mask 采样、Draw3 命令断言等非 F-019 差异归入本修复。

## 结论

未发现 F-019 实现的阻断性错误。`BarSurfaceScene::Impl::widgets` 自持稳定按钮，renderer 的 `svgMap/pngMap/barButtonSet` 清理不覆盖它们。新 `ResetWidgetDeviceCachesLocked()` 在 Scene mutex 内清理 Widget 的 SVG 与 PNG 上传位图（`Bar.Scene.cpp:738-747`），保留内容与按钮身份；显式 `ReleaseDeviceResources()` 和成功的隐式 target/epoch 重建都调用它（`:1959-1964,2035-2050`）。

Scene 对重建的预判与当前 renderer 复用谓词逐项一致：generation、context、target、GDI interop、target 宽高（`Bar.Scene.cpp:2041-2049`；`Bar.Rendering.cpp:106-115`）。renderer 先创建下一 context/target/interop，三项成功才丢旧资源（`Bar.Rendering.cpp:118-152`）；失败时 Scene 不清 Widget 缓存，仍可用旧 epoch 继续绘制。generation 是 RenderPipeline 单调递增的资源身份合同（`RenderPipeline.cpp:246-264`、`rendering-and-ui.md:1004-1009`），因此 COM 地址复用不会误判为同一设备；同 generation 换独立 device 不在该生产合同内。尺寸变化即使 generation 不变也清缓存。

PageControl 每次呈现前经 Scene `EnsureDeviceResources`（`PageControl.cpp:1034-1039`），GetDC/ReleaseDC/EndDraw 失败时经 Scene `ReleaseDeviceResources`（`:1211-1234`）；Whiteboard Freeze 同样在 `Whiteboard.cpp:158-160,204-213,282` 使用 Scene API。没有发现生产调用者绕过 Scene 直接重建其私有 renderer。

## 待补的验证证据

1. **P2，资源创建失败事务未被回归测试覆盖。** `Bar.EraserAttribute.Test.cpp:816-819` 传入宽度 0，`Bar.Rendering.cpp:109` 直接返回 `E_INVALIDARG`，未进入 `RecreateDeviceResources` 的 context、bitmap 或 interop 创建阶段。它能证明前置参数拒绝保留旧像素，不能证明“已创建部分新资源后失败”的事务。源码审查显示该阶段用局部 `ComPtr` 并在全部成功后才提交（`Bar.Rendering.cpp:123-152`），但任务若要求确定性失败注入测试，仍应补一个实际创建阶段的失败路径，或在记录中明确此项只经静态证明。
2. **P2，修复前红测的原始证据目前不可重查。** `scene-device-cache-fix.md:26` 记载修复前报告 `FAIL Scene icon renders after explicit release on new epoch`、`failures=1`，同时说明直接 PowerShell 调用读到 `$LASTEXITCODE=0`；对于 Windows GUI 子系统进程，该值不能替代等待后的进程退出码。当前 `Build/eraser-b/offscreen-results.log` 已是 `failures=0`，仓库内未找到该 Scene 红测的独立归档日志。修复后文档记载 `/m:1` Solution 构建退出码 0、`Start-Process -Wait -PassThru -WindowStyle Hidden` 测试退出码 0，现存报告与后者一致；独立复审无法从留存日志重新核对红测或两次 MSBuild 的完整命令输出。建议在任务记录中把红测标为“执行者记录、原始日志被覆盖”，不要把直接启动读出的 0 当作红测退出码。

## 回归覆盖与边界

- 测试经 `IdtMain.cpp:267-272` 在产品 UI 初始化前分流；`RunEraserAttributeOffscreenTest()` 初始化共享 WARP/D2D，但不建 HWND（`Bar.EraserAttribute.Test.cpp:85-100`）。新增 Scene 块使用第二个独立 D3D11 WARP/D2D device（`:731-745`），调用生产 `Configure/EnsureDeviceResources/Render/EndDraw` 并 CPU BGRA 回读（`:747-797`）。这是真实 D2D 资源域回归，不是复制缓存谓词的单元测试。
- `background.visible=false`、默认无文字，Scene 初始化将按钮 shape 的 `pct` 设为 0（`Bar.Scene.cpp:893-930`）；因此测试中首帧非透明像素断言（`Bar.EraserAttribute.Test.cpp:802-809`）可归因于 SVG。显式释放后比较完整 BGRA，隐式 epoch 切换和同 epoch target resize 比较旧区域像素（`:810-833`）；`rendered` 与 `EndDraw` 同时成功才进入像素比较。没有依赖 COM 指针地址相等或新旧对象地址不同。
- 测试没有主动构造 PNG Widget。当前 Scene 初始化只从 `iconResource` 创建 SVG，按钮默认 `iconKind=Svg`（`Bar.Scene.cpp:955-975`、`Bar.Button.cppm:126`）；PNG reset 是正确的防御性覆盖，但不能把本测试描述成 PNG 像素回归。
- 离屏测试不覆盖真实 PageControl/Whiteboard 的 HWND、GetDC、ULW、显示器切换或设备丢失驱动路径；这些仍属后续真机门禁。F-019 不应记为性能收益。新增回读只留内存像素；现有 `Build/eraser-b` 输出由 `.gitignore:17` 忽略。Scene/Test 差异 `git diff --check` 通过，两文件均维持原有 CRLF；Test 保持 UTF-8 BOM。
