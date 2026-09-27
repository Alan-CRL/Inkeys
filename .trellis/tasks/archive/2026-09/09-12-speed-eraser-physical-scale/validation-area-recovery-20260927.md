# 面积参考失配恢复验证（2026-09-27）

## 基线与日志边界

工作基线为干净 `bugfix/eraser` HEAD `c777b8dd9701c47c24b6fc8000bbd4c656e3011c`，原任务仍 `in_progress`。三份 Surface 日志的可见擦除快照为 Mouse 84、Touch 面积关 184、Touch 面积开 100 条；对应同接触光标与几何直径在输出精度内一致，`|2×actualDip−geometryDiameterPx|` 最大约 0.001px。详见 [研究记录](research/area-reference-mismatch-20260927.md)。这些是稀疏诊断，不作原始包重放。旧日志缺少锁存参考两轴值，不能确证 `contactId=55/gen=2` 的具体失配轴。

现有代码在相对参考检查失败后保留旧 `referenceWidth/Height`，每个后续离群样本均提前返回，不会进入初次候选；旧参考按 200ms 无效宽限和 180ms 衰减释放。先加入“初始窄参考 → 硬合法宽面积持续真实拖动应恢复”的测试：未改生产控制器时完整 `InkeysRepo.sln Debug|ARM64` 构建退出 0，headless 退出 1，且仅该新增断言失败。这是独立控制器反例，不是三份日志的逐点重放。

## 实现与安全边界

只在已锁存参考后的相对离群路径建立恢复候选。每个包先通过原单位、有限值、绝对范围与长宽比检查；候选须在合法采样间隔内持续真实移动 160ms，且两轴始终接近本轮第一个候选的固定锚点。滤波宽高仅用于正式接受后的新比较参考，不能随缓慢漂移带着确认窗口前进。硬无效、回到旧参考、静止、采样空档或真重连清空未完成候选；重连保留已经接受的原参考和辅助下限。首次 50ms 建立与原无效释放路径不变。

首次成功建立的面积下限另存为本接触不可抬高的上界。恢复后的下限取新合法面积计算值、首次上界和原配置上限的较小值；多轮恢复不能逐级扩大，新 Touch 则独立建立新上界。`ObserveContactArea` 在真实包区间已由 `AdvanceState` 积分后运行，因此正式恢复只能被后续真实移动消费，不追溯扩大旧点或无Move帧。

原诊断开关及约 250ms 帧级限频保留；`[TouchArea]` 新增参考两轴、当前/参考比例及失配轴、恢复候选与真实移动时长、首次上界、恢复次数、fresh/释放/候选状态和 `areaAboveB`。`areaActive=1` 可仅是低于 B 的释放尾端，不等于仍在擦出大于标准尺寸的面积洞；高频回归在测试内存中完成，不在原始输入热路径逐包写盘。

## 回归与产品结果

最终 headless 退出 0，`SpeedEraser failures=0`。新增回归覆盖：单次尖峰、交替宽高、缓慢漂移、96DIP以上/NaN/未知单位/异常比例、静止按压、无Move帧、缺包、重连前后候选、多轮恢复不抬上界、新 Touch、面积关、24/32/40 档以及 60/125/240/1000Hz 输入 × 30/60/120/144fps 帧。既有正常面积 39 DIP 参考、面积关闭、暖状态短快划、40ms高/120ms普通回缩和教室场景测试均通过；面积频率最差差异 0.598204%，原 sample/frame 最差 4.61275%。

隐藏窗口专项在 DComp/ULW 两种路径均通过新增恢复断言：先用 20×40 DIP 建立 50 DIP 首次下限；52×50 DIP 的原地相对离群样本使旧下限释放到约 16.24/16.22 DIP，实际约 17.46/17.36 DIP，`areaActive=1` 但 `areaAboveB=0`、恢复次数 0；随后真实拖动使比较参考变为 52×50 DIP、下限和实际/光标均为 50 DIP、恢复次数 1，光标、待用半径和最后真实几何点相等（后二者均为 25px）。恢复时保留旧历史半径，新增同位尺寸锚点；首次续画短段最大半径约 8.68/8.64px，未把旧粗尾直接拖进新段。现有隐藏产品用例的尺寸断点 UInk 保存/回读、Undo/Redo、面积开关及首点路径也没有新增失败。

最终隐藏测试退出 1，失败清单只有既有的四条 Window Service owner 断言：DComp/ULW 分别各有 `drawpad and presentation remain Freeze siblings`、`Host stop leaves hidden Window Service HWND intact`。整项不能记为通过；本轮未删除、跳过、归因环境或重构窗口所有权。此前验收中出现过不同的偶发入口/大屏断言，原因仍未证实，本次最终清单没有复现。

## 实际命令与待验收

- 用 `vswhere -latest -products * -property installationPath` 定位当前 Visual Studio 的 ARM64 原生 `MSBuild.exe`；同一 PowerShell 调用保存原 `Path` 内容，执行 `Remove-Item Env:PATH -ErrorAction SilentlyContinue` 后恢复单一 `Path`，设置 `MSBUILDDISABLENODEREUSE=1`，运行 `MSBuild.exe InkeysRepo.sln /t:Build /p:Configuration=Debug /p:Platform=ARM64 /m:1 /nr:false /nologo /v:minimal`。加上最后一条产品几何断言后的完整构建退出 0（输出在本会话工具记录；前一轮日志为 `%TEMP%/inkeys-area-anchor-build.log`）。没有修改工程、SDK、依赖或全局环境。
- `Build/ARM64/Debug/InkeysHeadlessTests.exe --no-window`：基线红灯退出 1（仅新增断言），最后源码退出 0；日志 `%TEMP%/inkeys-area-review-headless.log`。
- `Build/ARM64/Debug/Inkeys.exe --draw3-eraser-hidden-test` 以隐藏窗口进程运行、600 秒超时等待：最后源码退出 1，只有上述四条 owner 断言；日志 `%TEMP%/inkeys-area-review-hidden-stderr.log`。没有启动需人工关闭的 GUI 或使用 Computer Use。

Surface 真实指姿是否稳定恢复、屏幕笔和教室大屏等设备仍需分别人工测试；本轮合成与隐藏窗口验证不宣称完成实机验收。用户在验证后授权提交并推送本轮修改；任务保持 `in_progress`，不 finish 或归档。最终 `git diff --check` 退出 0；全部 11 个本轮文本文件保持原 BOM/CRLF 或以 UTF-8/CRLF 新建。完整构建产生的受跟踪 `Inkeys/PptCOM.dll` 已从起始干净基线的 HEAD blob 恢复原字节，最终无二进制差异。
