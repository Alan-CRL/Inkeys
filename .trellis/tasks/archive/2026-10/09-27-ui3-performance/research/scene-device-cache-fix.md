# UI3 Scene 设备缓存失效修复（2026-09-28）

## 范围与根因

`BarSurfaceScene::Impl::widgets` 持有稳定的 `BarButtonClass`。其中 SVG 图标的 `cacheBitmap` 和 PNG 图标的 `cacheBitmap` 属于创建它们的 D2D device。原有 `BarSurfaceScene::ReleaseDeviceResources()` 只调用 `rendererOwner.spec.DiscardDeviceResources()`；后者清理 `rendererOwner` 注册表中的 SVG/PNG/按钮，却不遍历 Scene 自己的 `widgets`。`SetWidgets`/布局切换可以保留这些稳定按钮，因此下一设备 epoch 可能沿用旧位图。

还核对了隐式重建入口：`BarUIRendering::EnsureDeviceResources()` 在 generation、context、target 或 target 尺寸变化时调用 `RecreateDeviceResources()`，三项新资源全部创建成功后才丢弃旧 renderer 资源。Scene 在该入口也必须同步清理自己持有的按钮缓存；失败时应保留旧缓存与旧 renderer 资源。

生产调用者审查：PageControl 的失败/`DeviceLost` 和注销路径调用 Scene 显式释放，Whiteboard Freeze 的失败与 shutdown 路径也调用显式释放；两者每次呈现前都调用 Scene `EnsureDeviceResources()`，因此 epoch 切换与容量变更可经隐式重建。未找到绕过 Scene 直接操作其私有 renderer 的其他生产入口。

## 最小实现

- `BarSurfaceScene::Impl::ResetWidgetDeviceCachesLocked()` 在现有 Scene mutex 下遍历 `widgets`，对按钮的 SVG 和 PNG 图标分别调用 `ResetCache()`；只丢 D2D 上传位图，保留 SVG 内容、PNG 解码像素、按钮身份与动画进度。
- `ReleaseDeviceResources()` 在 renderer 释放后清理 Widget 缓存。
- `EnsureDeviceResources()` 在调用前按 renderer 的 generation、target 尺寸及 context/bitmap/GDI 三项完整性判定是否需要重建，仅成功后才清理 Widget 缓存。无变化的帧不失效；目标创建失败不失效，因此旧资源仍可继续使用。

没有改变绘制参数、SVG raster 尺寸/颜色规则、脏区、光影或呈现帧率；该修复按正确性记账，不能当作性能收益。

## 无窗口回归与结果

回归加在生产 `Inkeys.exe --bar-eraser-offscreen-test` 路径，直接构造 `BarSurfaceScene` 和内嵌 `barSelect` SVG。epoch A 来自生产 RenderPipeline，epoch B 使用独立的 D3D11 WARP device 与 D2D device，均不创建 HWND。测试完成 `BeginDraw/Scene::Render/EndDraw` 后进行 CPU BGRA readback，比较完整像素或 resize 后原区域逐行像素；并覆盖显式释放、隐式 epoch 重建、无效 target 尺寸失败后保留旧资源。

| 阶段 | 命令 / 结果 | 证据 |
| --- | --- | --- |
| 修复前构建 | 原生 ARM64 MSBuild，`InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false`，退出码 `0` | 新测试编入 `Inkeys.exe`。首次 `/m` 构建退出码 `1` 但 minimal 输出没有有意义错误；同一源码改用 `/m:1` 串行构建通过。 |
| 修复前离屏 | `Inkeys.exe --bar-eraser-offscreen-test` | `Build/eraser-b/offscreen-results.log` 记录 `FAIL Scene icon renders after explicit release on new epoch`，`failures=1`。直接 PowerShell 调用的 `$LASTEXITCODE` 显示 `0`，因此失败判据采用测试报告，而不把该数值记为可靠进程退出码。 |
| 修复后构建 | 同一 Solution、Debug/ARM64、`/m:1`，退出码 `0` | Scene.cpp 编译及 Inkeys、PptCOM、InkeysHeadlessTests 链接通过；输出仅见 `IdtPlug-in.cpp` 引入的既有 `hashlib++` C4267 warning。 |
| 修复后离屏 | `Start-Process -Wait -PassThru -WindowStyle Hidden` 运行上述测试，进程退出码 `0` | 报告 `failures=0`；显式释放后的新 epoch、隐式切换、失败事务、resize 像素断言均通过。 |

构建前在同一 PowerShell invocation 执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue`、设置 `MSBUILDDISABLENODEREUSE=1`，并由 `vswhere` 解析本机 ARM64 MSBuild。`git diff --check` 与 Scene/Test 原有编码和 CRLF 检查通过。

## 未验证与后续

无窗口回归证明 WARP/D2D 资源域和 BGRA 输出；它不包含 PageControl/Whiteboard 真实 `GetDC`、ULW、DWM、Office 或 Win7 SP1 + KB2670838。真实设备丢失、显示切换和窗口像素仍需按任务人工门禁验证。此修复没有 UI3 分阶段性能采样、三轮前后对照或收益结论。
