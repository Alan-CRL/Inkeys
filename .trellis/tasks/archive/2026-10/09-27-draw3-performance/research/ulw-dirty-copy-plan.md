# ULW dirty 拷贝与 alpha 检查：测量方案

状态：2026-09-28 静态候选，未测得瓶颈，未改生产代码。

## 当前生产路径

`Draw3.TransparentPresentation.cpp::UlwPresenter::Present` 在 GPU dirty 子矩形复制到 staging 后 Map，只把脏区逐行 `memcpy` 到 top-down BGRA DIB；Unmap 后又遍历 DIB 同一脏区每个像素，计算 `allZeroAlpha` 与 `premultipliedAlphaValid`，再调用 `UpdateLayeredWindowIndirect`。后两项用于成功呈现观察/选择态边界，不能为省时删除；DIB 像素、alpha=0 和 dirty 合同也不能改变。`Map` 可能等 GPU，不能将它的耗时算为纯 CPU 拷贝。

## 冻结的受限基准

- 从现有 `Present` 抽出同一生产 dirty 拷贝/检查函数，显式无 HWND 入口在 Release|ARM64 上以固定 RowPitch、DIB stride、BGRA premultiplied 输入调用该函数。场景至少有 1920×1080 全量、256×256 局部、窄长 dirty、全透明、半透明和彩色，以及故意无效 premultiplied 像素；每场景预热、至少三轮，每轮不少于 11 个原始测量块。测试比较完整 DIB 字节、两项观察布尔值及脏区外原内容，不改变输入格式或减少必要检查。
- 每块记录样本量、CPU 拷贝+扫描耗时和线程周期（可得时），报告 median/P95/轮间噪声；小样本不报可靠 P99。采样期间停止编译、Git 扫描和其他基准。保留修补前后原始数据于忽略 `TestResults/release-hardening/`，不挑最快轮。
- 候选仅在数据证明该 CPU 段显著时尝试“逐行复制后在 staging 源行仍热时检查同一像素”，避免第二遍读取 DIB；DIB 字节和观察结果必须逐位等价。收益未超噪声或增加复杂度则撤销候选。

这个无 HWND 测量不能覆盖 D3D `Map` 等待、真实 `UpdateLayeredWindowIndirect`、DComp/ULW 成功 Present、Win7 Hardware/WARP 或光学延迟。它只支持 CPU 子段成本结论；完整用户体验仍按父任务矩阵人工验收。保持用户已实测的 Win7 `FLIP_SEQUENTIAL`，两种 DWM 透明方案均禁用。
