# 调查验证记录

日期：2026-09-22。源码基线：`94e07b2599adab9de4286aa5fb7526e2c1c681f6`。任务状态 planning。

另对 `ab023a17` 的原始文件做只读比较：颜色块中间 `ft.SetTar(1.0)` 同样有22处，exact非零平移拒绝和scene无条件收集hooks也已存在。因此这三个发现不是本轮HEAD新增本地代码造成，也不是只适用于新的国际化改动。

## 已执行

| 验证 | 执行对象 | 结果与局限 |
| --- | --- | --- |
| 真实动画模块目标冲突 | 从当前 Metrics.cppm、Animation.cppm、Animation.cpp 新编译，再由 animation_target_probe.cpp import | 编译/执行均 exit 0，无 warning；第 23 帧归一 ft=4.117780523，单目标对照误差5.55e-16，最终收敛。执行真实动画函数，但不是完整 RenderLoop/GUI。 |
| 几何包含/峰值保留 | geometry_probe.cpp include 当前 WindowGeometry.h、BottomDock.h | 编译/执行均 exit 0，无 warning；确认高水位保留、光照 active 时 viewport 不收缩、独立包络组合可超 target。未调用 ULW，不能判定真实窗口返回错误。 |
| 呈现失败退避 | scheduler-recovery-check.cpp include 当前 PresentDecision.h | 编译/执行均 exit 0，无 warning；124 次60Hz回调，实际尝试8次、跳过116次、动画累计0.133333s。时钟部分标为源码同构模型。 |
| 分片数量 | lighting_slices_probe.cpp 内原样提取两个当前生产函数体，使用 fake context | 编译/执行均 exit 0，无 warning；15个受光对象135/375次FillOpacityMask。无真实D2D耗时或Gaussian创建。函数文本hash记录在光影专题报告。 |
| 关闭轨迹与缓存键 | input-animation-ft-model.py、lighting_cache_model.py | 均 exit 0；公式与真实动画 module 对照一致；首次关闭新增6/7父mask键，重复固定轨迹可命中。 |
| DC 互操作小实验 | dc_interop_probe.cpp 创建真实 WARP+D2D device/bitmap/context，未创建 HWND | 编译/执行均 exit 0，无 warning；ABBA、各预热20/采样80、3种面积，总P50约0.09–0.17ms。没有ULW、真实光影或用户故障现场。 |
| 已有 headless 基线 | `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window` | exit 0，结尾 `PASS animation correctness`；测试二进制为本机已有文件，本轮未重新构建，不代替当前完整solution编译。 |

现有 headless 二进制在执行前读取的信息：大小 10,799,616 bytes，本地 LastWriteTime `2026-09-22 17:16:51`。无窗口参数由 `InkeysHeadlessTests/animation_tests.cpp:1487` 的入口解析，窗口测试受 `runWindowTests` 控制。未使用任何 visual-test 参数。

## 工具链与复跑

已先确认主工程使用 MSBuild/Visual Studio，且存在独立 headless 项目；调查探针只复用现有 MSVC 编译器，不创建新 solution/CMake 或修改项目文件。

Visual Studio 安装通过本机 `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.ARM64 -property installationPath` 查询。调查实际使用：

- `C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostarm64/arm64/cl.exe`
- 同工具目录下 `include` / `lib/arm64`
- Windows SDK `10.0.26100.0` 的 `Include/{ucrt,shared,um,winrt}` 与 `Lib/{ucrt,um}/arm64`
- 单 TU 探针：`/nologo /std:c++20 /EHsc /utf-8 /W4`
- 动画 module 探针：另加 `/O2 /MT /permissive-`，每个 `.cppm` 用 `/c /TP /interface /ifcOutput`；Metrics/Animation module interface 与 implementation 均从当前源码编译，用 `/reference Inkeys.UI.Bar.Animation=<Animation.ifc>` 和 `/reference Inkeys.UI.Bar.Metrics=<Metrics.ifc>` 连接。
- D2D 探针：另加 `/O2`，链接 `d3d11.lib d2d1.lib dxgi.lib ole32.lib`。

主代理生成的 exe/obj/ifc 位于本轮 `%TEMP%/ui3-animation-target-probe` 与 `%TEMP%/ui3-animation-stutter-geometry`；它们不属于仓库 diff。子代理临时 probe 编译目录已按其报告移除。保留的研究源、CSV与原始stdout均在本任务research目录，可用于复核公式、调用顺序和数值。不同开发机应重新从安装信息定位工具链，不把以上路径写入产品配置。

Python 模型可直接复跑：

```powershell
python .trellis/tasks/09-22-ui3-animation-stutter-investigation/research/input-animation-ft-model.py
python .trellis/tasks/09-22-ui3-animation-stutter-investigation/research/lighting_cache_model.py
```

## 未执行

- 没有修改或完整构建 Inkeys 主 Solution。本轮是调查，产品实现尚待批准；不能宣称修复后的主程序已经编译通过。
- 没有启动 Inkeys、设置窗口、PPT、浏览器或 Computer Use，也没有创建隐藏 HWND 进行 ULW 实验。
- 没有用户故障现场的 ETW、栈、真实帧时间、CPU/GPU占用或错误码；未证明同一偶发故障可复现或已经消失。
- 没有把容量异常、source越界模型、exact gate或Settings同步Present分别冒充唯一根因。

## 文档与差异检查

结束前验证 task.json/jsonl 可解析、任务仍 planning、所有相对 Markdown 文件链接可解析、研究文件 UTF-8有效；检查 git status/diff，变更仅限本任务目录。主程序源码、现有 tests、项目配置与 spec 均保持原样。

实际检查结果：27个文件，UTF-8/JSON/JSONL/本地链接/文档空白检查全部通过；tracked和staged diff均为空，`git status --short` 仅显示本任务新目录。没有exe/obj/ifc/pyc进入任务材料。任务当前路径已指向本任务，status仍为planning，未执行task.py start。
