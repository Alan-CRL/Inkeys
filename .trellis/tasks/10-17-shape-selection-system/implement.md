# 执行顺序

用户已明确授权本方案及任务创建/实施，无需再次批准。
1. 总任务/Phase0/Phase1与五个实施子任务建立文档与上下文。
2. 共享 Shape 核心、几何查询、命令、缓存与纯测试；发布稳定 API。
3. 并行独立 shader/renderer 与共享 UInk adapter；主会话负责生产 InkStroke/Controller/白板接线。
4. 真实生产 renderer 离屏测试；复用既有 no-window 测试入口。
5. 定位 ARM64 MSBuild，同一 PowerShell Remove-Item Env:PATH + MSBUILDDISABLENODEREUSE=1，构建 InkeysRepo.sln Debug|ARM64（≥5分钟）；必要共享测试 solution。
6. CPU、持久化、GPU、旧工具回归；检查diff/编码；独立 trellis-check 全范围审查。
7. 更新spec、validation与任务状态。保留未提交改动，不运行自动commit/archive，不push。

## 测试
类型样式独立、对象身份、非法参数原子性、凹多边形/复合/命中/控制点/缓存、旧四形、Undo/Redo/Clear/取消、旧新UInk/未知版/损坏/重复身份、桌面/PPT/白板隐藏历史；GPU四线型/三样式/半透明接缝/相位/tile一致/批次/NO_OVERWRITE回退/资源失败。仅离屏资源，不创建HWND。

P2实施11类单笔/UI3/预测。逐点工具移至P3提示之后的独立任务。

## P2集成完成
11类别UI3单笔/预测/保存已实现；native/shared ARM64构建及GPU306/UI6238/CPU/持久化/Headless均通过。验证见Phase2 validation.md。下一步实施P3选择与提示窗口，再实施独立逐点工具。
