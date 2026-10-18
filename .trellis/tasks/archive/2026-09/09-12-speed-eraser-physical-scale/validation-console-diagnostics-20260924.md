# 输入与橡皮控制台诊断扩展验证（2026-09-24）

## 范围和保留项

在 `bugfix/eraser` 的 `9535d40364ebfdef68d20e160fdf880c9f6cea62` 上继续原 `speed-eraser-physical-scale` 活动任务。只扩大原实验开关的诊断范围并改可见名称，保留 `Experimental.Inkeys3.ConsoleOutput.TouchArea` 持久化键及“下次启动生效”语义。笔速参数、面积判定、EDID 业务标尺、RTS、轨迹采集和渲染未改。用户后续授权提交并推送本轮改动；任务仍不 finish 或归档。

三语言名称现为输入与橡皮诊断控制台输出；zh-CN 基准经 `Scripts/i18n.ps1 sync` 同步快照，en-US/zh-TW 翻译标记已补齐，最终 `pwsh ./Scripts/i18n.ps1 check` 为两语言 330/330、退出 0。生成 key header 没有差异。

## 输出合同和实测样例

- `[EraserDisplay]` 在显示快照或诊断配置发布时输出：EDID 解析状态/版本/原始厘米、业务可用物理厘米及失效原因、拓扑、活动分辨率、有效 DPI、DIP/px 和显示代际。活动分辨率与 EDID 原始时序分开；隐藏测试明确标 `synthetic=1`，显示 320×240、96DPI、`edidStatus=Unavailable`、`physicalReason=NoMonitor`，未伪造尺寸。
- `[EraserInput]` 复用约 250ms 帧级限频，附帧秒数、选中 contact 的 ID/代际、真实设备与来源、输入画布坐标、可见光标坐标、工具、面积、目标/实际 DIP、光标/几何像素直径，以及报告速度和清扫资格速度。普通画笔或隐藏光标用 `-1`/`N/A` 明确标记无效，旧 `[TouchArea] contact` 继续表示橡皮接触。
- 隐藏注入观察到手动大屏 Touch `inputCanvasPx=(60,120)` 与同 contact 光标相等；固定 Touch 在 `(60,170)` 报告 `targetDip=actualDip=cursorDiameterPx=geometryDiameterPx=32`。普通 Pen 在 `(110,100)` 报告 `device=Pen`、`eraserKind=N/A`、不可见光标为 `-1`，未把默认 32 DIP 误作橡皮尺寸。Mouse/Pen/Touch 来源和坐标的新增隐藏断言未报失败。

这些是稀疏快照，能对照速度、目标、实际擦除宽度随动作变化；不能把相邻行差值当成原始硬件逐点路径，也不能据此推断硬件回报率。未在真实 Surface 或教室设备取得 EDID/手感样本，真实设备调测待用户打开该开关并重新启动后进行。

## 执行结果

使用当前 VS 安装中的 ARM64 原生 MSBuild，在同一 PowerShell 调用内保存 Path、移除重复 PATH 并恢复原内容，仅对本进程设置 `MSBUILDDISABLENODEREUSE=1`：

```text
MSBuild.exe InkeysRepo.sln /m:1 /nr:false /p:Configuration=Debug /p:Platform=ARM64 /v:minimal
Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window
Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test
pwsh ./Scripts/i18n.ps1 check
```

完整 solution 构建退出 0，headless 退出 0，i18n check 退出 0，`git diff --check` 通过。专项隐藏测试通过新增设备/坐标/尺寸断言，但整项仍退出 1：DComp 与 ULW 各有 `drawpad and presentation remain Freeze siblings`、`Host stop leaves hidden Window Service HWND intact` 两条既有 Window Service owner 关系断言失败。它们在上一轮同样可复现，未归因为此次诊断或擅自修改窗口架构。没有打开交互式 GUI；构建生成的 `PptCOM.dll` 差异在确认初始工作区干净后恢复原字节。

本机日志分别位于 `%TEMP%/inkeys-eraser-console-verified-build.log`、`inkeys-eraser-console-verified-headless.log`、`inkeys-eraser-console-verified-hidden-stderr.log`。
