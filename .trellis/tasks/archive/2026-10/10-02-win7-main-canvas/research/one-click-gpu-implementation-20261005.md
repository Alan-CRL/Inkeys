# 单 CMD GPU 序列实现（2026-10-05）

本实现沿用同一 Win7 主画布任务，仅扩展显式独立 GPU 自检；没有替换产品 shader、修改生产 HLSL/原 8 个 CSO/SHADER301–304、构建配置或输入行为。没有运行正常产品 GUI、完整 solution build、commit 或 push；主会话后续负责构建和检查。

## 改动与序列

- `Draw3.HiddenWindowTest.cpp`：保留原 6 项与 7 项诊断的顺序、检查和退出合同。新增独立 `sequence_cases=5/sequence_failures`；原四项正式检查和 device-removed 仍决定进程退出，诊断成功不覆盖它们。
- `Draw3.PixelDiagnosticShaders.h`：原 ConstantOutput/ValidateFields/CursorOnly 数组逐字节相同；追加 3 个完整程序变体和预期生产资源 SHA。
- `Generate-Win7-PixelDiagnostic.ps1`：从当前生产 HLSL 与 ink.hlsli、laserParticleCommon.hlsli、EraserGripVisual.h 克隆 ASCII 临时文件，FXC ps_5_0 /O3 生成；正式源及构建链不改。临时源、反汇编和元数据证据位于下述 artifact 目录。

五项都是同一独立 offscreen target、生产 VS、正式 Cursor 上传和正式 blend，逐项 clear/readback，复用已有每 query 3 秒的界限。原 13 项后先立即原 PS retry，再创建新变体，避免先创建 PS 带来的额外暖机混杂；每项完成恢复 renderer texture/RTV/blend/PS。

| 顺序 | case | 唯一对照 |
| --- | --- | --- |
| 1 | sequence.original_after_cursor_only | 精简 PS 之后重新绑定原生产 PS 对象 |
| 2 | sequence.full_recompiled | 完整生产源原样 ASCII 复编、新 PS 对象 |
| 3 | sequence.full_cursor_entry_constant | 原完整 Cursor 4..6 内，type4 在数学/discard 之前返回 Add=Retain=0.5；其它完整路由保留 |
| 4 | sequence.full_cursor_ahead | 完整 Cursor 4..6 代码块仅移动到 Laser 9..10 之前，其它源码保留 |
| 5 | sequence.original_final | 再次重绑定原生产 PS 对象 |

哨兵沿用 36×36 quad 的常色验收（1296 非零像素、中心白色约128 alpha、透明角），不把只输出常色的小程序冒称成完整路由探针。缺失变体报告 ERROR；各结果单独输出且新增序列失败不隐去原正式失败。

## 已执行静态/FXC 验证

从安装 SDK 目录定位原生 ARM64 FXC `Windows Kits/10/bin/10.0.26100.0/arm64/fxc.exe`；扩展生成器两次成功退出 0，后一次核 compiled sentinel 路由与 Cursor-first 的真实指令顺序。六个数组与对应生成 CSO 完全一致，原三个数组与修改前 manifest 完全一致。

三个完整变体保留原生产 PS 的全部 17 条 resource rows、ISGN/OSGN register/mask 以及完整 constant/linear 插值声明；history Texture2DArray、texture sample、其它工具路径仍存在。真实反汇编显示 type4 比较后直接 mov o0/o1=0.5 并 ret；前移版先执行 4..6 gate、Cursor 数学/输出，再出现 9..10 gate。不是 constant-only 折叠。

**原样复编控制与当前生产 PS 的 ISGN、OSGN、SHEX、STAT 逐字节完全相同。** 整体 SHA 不同来自 RDEF metadata；若原样复编在 Win7 恢复，仍需解释 shader 创建对象/metadata/JIT 状态，不可称为已修复指令。metadata 差异不自动说明执行指令差异。

完整证据：`C:/Users/alan-/.codex/visualizations/2026/10/03/01a100c6-aa67-7cc3-9d01-6cda328f70c3/win7-one-click-20261005/gpu-fxc/`，其中 `full-source-evidence.json` 保存生产源原始 SHA、各变体临时 ASCII 源 SHA/profile/bytecode/chunk SHA；`dxbc-chunk-evidence.json` 保存 chunk 长度及 hash；`gpu-freeze-identity.json` 保存修改文件与数组身份。

文件保持原无 BOM、CRLF；头/生成脚本 ASCII，C++ UTF-8。`git diff --check` 通过。一次默认 PowerShell host 在脚本开始前因 ApplicationInsightsTelemetry OutOfMemoryException 退出，改用缓存 pwsh 的静态核对成功；没有代码或系统配置绕过。

## 结果解释与待验证

- 原 PS retry 恢复：先看序列/暖机状态，不应先归因某条数学指令。
- 原 PS 一直失败但入口哨兵成功：支持完整程序中的 Cursor 入口可到达、写入可工作；完整变体布局也变化，仍不能唯一证明原数学或 discard 违规。
- 前移版成功：支持路由顺序/字节码/JIT 布局差异，不能单次证明 Win7 驱动错误或立即作为全部工具修复。
- 三个完整变体仍失败而精简成功：完整程序执行差异继续细分；不能由 API 成功宣称出墨。
- 此独立 WARP 探针不代表 VMware 正常硬件应用。普通与 full-ULW 的视觉观察和实际 source/DIB/耗时由采集器补齐。

Win7 两机全部结果待用户实测；C++ 编译/本机 GPU 集成由主会话接续。

## 冻结身份

- `Inkeys/Inkeys/Drawing/Draw3/Draw3.HiddenWindowTest.cpp` SHA256 `731C2EE8FA89B680D793550698696587EE73974A2DBB8C1F830588C2C6480CAF`
- `Inkeys/Inkeys/Drawing/Draw3/Draw3.PixelDiagnosticShaders.h` SHA256 `CACA1C9DA939786F07EC4BE44D2300C325CC78BDC31EFD798DA5F7CE99DA74FE`
- `.trellis/tasks/10-02-win7-main-canvas/research/Generate-Win7-PixelDiagnostic.ps1` SHA256 `6CCB34B53BC4D58885FB1BAB71389C87C3173CFD76C8F1C7F9EA28A255469BB3`

| 变体 | DXBC 字节数 | SHA256 |
| --- | --- | --- |
| FullRecompiled | 28748 | 3D4701811FF9A5F556C408FA4BC9885444697C793A8059EF63C9A92626E1CE85 |
| FullCursorEntryConstant | 28860 | A728EEB1FCE900D43398D03F2D881E2D759EB09E969B49711294D0CD76B5FEAF |
| FullCursorAhead | 28748 | 2889BB484C69C9AF5B9B5B71AAF981C923512E7D3B60690C0ECC8399EF65F647 |
