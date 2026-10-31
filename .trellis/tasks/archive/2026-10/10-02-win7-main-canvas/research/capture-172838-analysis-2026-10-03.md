# Win7 完整采集 17:28:38 分析

本次运行：Windows 7 SP1 / PowerShell 2；正常退出0。identity 中 EXE SHA256 为 1E41D0D5EAAD4FB5D98D0FB0E840398A4F380F4A0DAB0FBE6E2DE2DF007BF7FA，与已交付候选一致。采集来自 G:/Inkeys/Inkeys-Win7-Input-Pixels-x64-Release/Inkeys-Win7-Input-Pixels-x64-Release/20261003-172838-167，原文件只读保留。

## 核心结论

本次鼠标复现已经证明 RTS 接收、解码、发布和 controller 消费。决定性的异常发生在 ULW 之前：采样的 GPU staging 读回源区域全透明，最终 DIB 只增加 Primary alpha=1 命中底层，ULW 成功提交这些空内容。优先调查共享绘制管线与纹理读回；尚不能证明具体 shader、Map、混合、viewport 或 Copy 操作为唯一根因。

## 证据

| 层 | 本次结果 |
| --- | --- |
| 鼠标光标消息 | 5483条，全部 accepted=1 |
| RTS Down | arrival13 / published13，全部 decoded=1 |
| RTS Up | arrival13 / published13，全部 decoded=1 |
| RTS Packets | 8条限频记录，全部 published；没有失败 reason |
| Cursor队列 | 没有 dropped 记录；从启动到退出完整采集 |
| controller contact | contactGen1..13；Pen2、SolidLine2、Eraser2、HardPen7 |
| 文档 | AutoSave clear strokes=13 bytes=17783；不能拿序列化笔画数替代GPU像素 |
| ULW像素 | 15个采样，sourceAlphaNonzero、sourceRgbNonzero和sourceMaxBgra均为0；Primary13、Selection2 |
| 最终DIB | Primary每个采样的全部像素alpha=1、RGB=0；Selection保持alpha=0 |
| ULW API | 15次采样全部 api=success / error=0 |

源采样是 CopySubresourceRegion 后 MapREAD 的 staging 数据，并非额外直接测量 GPU 原纹理。全零只覆盖这些采样提交的dirty区域，不能写成所有帧、所有时刻整纹理均全零。

决定性橡皮帧：console.stdout.txt:16650 读回 dirty=(948,938,984,974)，1296像素全部原始 BGRA=(0,0,0,0)；:16651 frame tool=3 primary=1；:16652 白色32px圆环，位置966,956、opacity0.5；:16731 同dirty present success=1。脏区确实覆盖应有视觉的位置，不能解释为只采到正常空白区。

普通笔第一次 Down :689 published、坐标559,909；:690 同位置 dirty=(554,904,564,914) 源透明；随后 :4679 dirty=(554,779,1082,914) 71280像素仍透明。stderr :15 起实际活动contact位置有效，表明消费链已经走到runtime。

本次没有 Laser4 阶段：不将之前报告的激光笔症状冒充此次实测。工具1为HardPen，不是Highlighter。

## 源码核对与剩余范围

ULW staging使用同尺寸BGRA8、相同源/目标dirty偏移，MapREAD成功后按RowPitch读源，合成前统计；当前静态检查未发现采样错区或底层alpha覆盖源统计的问题。

Renderer从同一GetBuffer(0)对象创建backBufferTexture/RTV；ULW分支没有每帧swapchain Present，静态逻辑未发现flip轮转读取旧别名。橡皮直接写backbuffer、绕过墨迹历史与L1/L0，说明单一历史cache故障不足以解释这次橡皮源全零。必需shader/resource创建有失败处理，viewport设置/预热恢复路径完整；创建成功仍不能证明Win7实际Draw写像素。Cursor两个Map失败静默返回仍是诊断盲点。

下一最小分界应在同一Win7设备上比较：已知颜色ClearRenderTargetView+读回，正式filled cursor draw+读回；同时核验viewport、RTV资源身份、Map结果和deviceRemovedReason。若clear读回正确而正式draw为空，则缩至上传/shader/raster/blend；若clear也为空，先查资源/读回。此GPU自检尚未实施，不宣称已定位唯一根因。

详细输入证据见 input-pixels-evidence-2026-10-03.md；源码核对见 win7-gpu-zero-2026-10-03.md。本轮仅日志/源码分析，未新增代码修改、构建或GUI运行，任务保持in_progress。

## 原始文件 SHA256

- console.stdout.txt: 1C0258E35DD1F18FA12B8649F06CFD283CD066AC851CF997CCBCDD6FC244B3D0
- console.stderr.txt: 627F49B7DF0B4A44D87E1D3CE79B8118779B83B5A5BD431CB2B088414C8A4848
- identity.txt: 5FC15D5F64C633E86043ABEC51E29E0F2ACAAE35C10D48771FE7E8D6A0721B8C
- idt1791019718760.log: 069FABAF2390613E8C020C02E55B0D18870D5F1A728106AA166549736A6B5344

末尾校验：三份生产源码SHA与候选manifest一致，diff check通过。复核附件哈希时G:已不可访问（Test-Path G:/为False），故末尾哈希复核未完成；上表SHA为首次读取时计算，调查过程中未执行任何G:写入。
