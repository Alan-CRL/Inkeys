# Touch 场景曲线修正：本轮验证（2026-09-23）

## 恢复与证据边界

原任务 `speed-eraser-physical-scale` 保持 `in_progress`，本会话经 `task.py start 09-12-speed-eraser-physical-scale` 选中。初查工作区干净，实际分支 `bugfix/eraser`，基线 HEAD `94e07b2599adab9de4286aa5fb7526e2c1c681f6`。任务元数据的历史分支 `feature/eraser` 已存入 `meta.historical_branch`；旧 `base_branch=main` 仅保留 PR 目标历史，不用于切换开发基线。本轮不建分支/工作树；用户后续授权提交改动，任务仍不推送、finish 或归档。

教室实机日志是间隔快照而非原始轨迹。用户“约 10cm/s”是估计；日志 270.498、321.777、282.742、432.376 mm/s 是控制器读数；“普通擦除”是体验判断，三者不写成同一测量。后半段高速峰值没有逐段动作标签。噪声合成回归不证明本机根因。本轮确定的源码缺口是可靠物理 DirectTouch 不论 Laptop/LargeScreen 都走 30/90/60/250 mm/s，EDID 正常时 LargeScreen 不参与曲线。

## 实现与有效参数

- `Draw3.SpeedEraser.h/.cpp`：真实响应模型、动作标尺和 Touch 场景分开；可靠物理/手动尺寸长边在 320–1200 mm 间用 smoothstep 形成 0..1 权重。显式 Laptop 上限 0.25、显式 LargeScreen 下限 0.25；内部 Automatic 仅在明确请求时自动取可靠尺寸，未知尺寸退 Laptop/DIP。Surface 约 28cm 的 Laptop 端点仍为 30/90/60/250 mm/s，教室约 139cm 的 LargeScreen 为 30/350/250/1300 mm/s；中间三项统一插值，fine 始终 30 mm/s。
- 无可靠尺寸时保留 LargeScreen 的 100/120/80/400 reference DIP/s 经验路径和 Laptop 的 100/240/160/700 DIP/s 路径；无效映射、手动尺寸和 ForceUnavailable 不冒充实测 mm/s。固定橡皮直接旁路。24/32/40 仅改变 0.5B/B/5B DIP；0.85/1/1.15 仅温和调整清扫动作，资格与目标仍用同一组阈值。
- `DrawingController`/`Host`：帧级诊断发布请求场景、有效 profile/强度/来源、单位、四阈值、B/增益、50ms 报告速度、实际清扫资格速度、140ms fineSpeed、资格/证据、目标/实际和对应 contact 的最终光标/几何。Touch 的 cursorPx 不再误取光标列表首项；仍沿用已有 250ms 限频输出，不加逐包 I/O。
- 精细低速平台、迟滞、时间常数、面积转换/下限、Surface 端点、Mouse/ExternalPen/TouchPad DIP、ScreenPenHybrid 补偿、会话及保存链路均未改动。旧跨场景同 mm/s 同 DIP 断言改为同场景单位正确性与各场景普通/清扫区可用；原历史验证文件未覆盖。

## 公式、控制器与合成场景

先加红灯测试并构建完整 solution：MSBuild 退出 0；旧实现 headless 退出 1，共 11 项新断言失败，实际大屏 enter/exit/large=90/60/250，300 mm/s 目标已达 160 DIP。改动后最终 headless 退出 0。详见 [CSV 对照](research/touch-scene-curve-20260923.csv)：合成 139×78cm、1920×1080、144DPI、B=32、中灵敏度、面积关闭的 3 秒局部圆弧回放，100–322 mm/s 稳态为 32 DIP，400 为 32.416，432.376 为 33.114，600/800/1000/1300 约为 42.159/67.153/109.401/160 DIP。这是公式及控制器回放，不是实机动作的精确重建。125Hz 的 20 秒 432.376 mm/s 局部持续轨迹未因时间累计触顶；短折返、慢快慢圆弧分别覆盖普通保持和放大后回落。

| 合成长边 cm | Automatic 权重 | enter / exit / large mm/s | 低于 enter 的普通 DIP | 快于 large 的持续清扫 DIP |
| ---: | ---: | ---: | ---: | ---: |
| 28 | 0 | 90 / 60 / 250 | 32 | 160 |
| 40 | 0.0233 | 96.06 / 64.43 / 274.46 | 32 | 160 |
| 55 | 0.1692 | 134.00 / 92.15 / 427.69 | 32 | 160 |
| 70 | 0.3984 | 193.57 / 135.69 / 668.28 | 32 | 160 |
| 90 | 0.7306 | 279.95 / 198.81 / 1017.11 | 32 | 160 |
| 120、139、200 | 1 | 350 / 250 / 1300 | 32 | 160 |

这些是明确宽高的合成场景；两端和 0.25 场景先验是首版经验标定，不能证明真实 20–32 英寸设备或大屏已达最佳手感。测试覆盖 1080p/1440p/4K、96/120/144/192 DPI、横竖屏、节点两侧、手动尺寸/EDID、映射丢失、ForceUnavailable、60/125/240/1000Hz 和 30/60/144fps。相同中等物理场景的 DIP 输出在显示配置间保持 5% 内；未将不同场景的同 mm/s 结果强求相等。原精细量化/稀疏 1728 组为零失败；既有鼠标/笔、点擦、idle、面积、重连、Undo/Redo、UInk 与配置测试继续通过。面积开启另验证有限辅助下限允许高于 B。

## 实际命令与产品接入状态

在同一 PowerShell invocation 中用 `vswhere` 定位当前 Visual Studio 的 ARM64 原生 `MSBuild.exe`，保存原 Path、移除重复 PATH 后恢复单一 Path，设置 `MSBUILDDISABLENODEREUSE=1`，执行：

```text
MSBuild.exe InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal
Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test
```

最终完整 solution 构建退出 0，headless 退出 0；构建仍有既有第三方和测试编译警告，未为清零警告修改无关代码。隐藏产品测试使用 `Start-Process -WindowStyle Hidden -Wait -PassThru` 得到真实退出码 1；另用独立 `ProcessStartInfo` 和沙箱外隐藏进程复核，均仅出现同一四条断言：DComp/ULW 各自的 `drawpad and presentation remain Freeze siblings` 与 `Host stop leaves hidden Window Service HWND intact`。这些检查位于橡皮场景前后的 Window Service owner 关系；新增手动大屏 Touch、接触场景锁存、下一接触切换、普通直径及光标/几何断言未报失败，现有面积/保存等橡皮断言也未报失败。但由于整项退出 1，产品接入只能记为“相关橡皮断言未失败，完整隐藏测试未通过”；不能将此断言故障认定为环境问题或改动窗口架构来掩盖。未运行交互式 GUI 或 Computer Use。

最终限频产品诊断在合成手动 139×78cm 接触中显示 `LargeScreen / Classroom / ManualCalibration`、权重 1、`30/350/250/1300 mm/s`；移动期间抓到的短窗报告速度约 0–138 mm/s，`sweepSpeed=0`，目标 32 DIP，实际由 Touch 小起步逐渐到约 31.6–32 DIP，光标与几何相等。下一接触显式 Laptop 解析出权重 0.25、`30/155/107.5/512.5 mm/s`。隐藏测试这段异步消息的速度未覆盖日志所述 270–432 mm/s，因此该范围的数值证据来自公式和控制器合成回放，尚无产品入口或实机的同速观测。

完整命令输出保存在当前机器的 `%TEMP%/inkeys-touch-profile-final-trace-build.log`、`inkeys-touch-profile-last-headless.log`、`inkeys-touch-profile-trace-stderr.log`；沙箱外复核日志为 `inkeys-touch-profile-hidden-elevated-stderr.log`。构建生成的追踪内 `PptCOM.dll` 差异已在确认初始干净后恢复原字节，未改工程依赖或用户文件。

## 人工验收仍需进行

Surface：选 Touch/Laptop、B=32、中灵敏度、面积关闭，依次做极慢精擦、普通局部拖擦、明显快扫、短点擦、停止再续擦；确认小屏仍容易进入原清扫区且不抖。教室设备：选 Touch/LargeScreen、面积关闭，用平常书写范围反复普通擦除，再逐渐加速清扫，观察 32 附近、连续中间尺寸和持续快扫接近 160；随后开面积辅助检查慢拖下限与误擦风险。两台设备均记录 `requestedDeviceMode/profileSource/motionSource/unit/fine/enter/exit/large/B/speed/sweepSpeed/targetDIP/actualDIP/cursorPx/geometryPx`，并给普通/清扫片段加动作标签。其他尺寸只能标为“合成/自动验证通过，手感待实测”。
