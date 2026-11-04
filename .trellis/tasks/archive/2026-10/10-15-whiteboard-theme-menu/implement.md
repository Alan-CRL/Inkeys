# 实施清单

- [x] 阅读prd和本层spec，确认文件所有权及接口。
- [x] 实现批准行为，保留编码换行；新增资源/编译文件告知主会话。
- [x] 添加生产逻辑驱动的无窗口回归，包含失败与内存约束。
- [x] 交主会话统一ARM64 solution构建，避免并发MSBuild。
- [x] 独立Trellis check审查并修复；记录局限及真实验证结果。

用户已审阅并以PLEASE IMPLEMENT THIS PLAN明确批准本prd/design对应方案。禁止commit/push。

## 实际实现与检查

实现与独立检查记录见 `research/check-review.md`。已实现同 HWND 菜单、六色色板、三种共用纹理预览与背景、标准 More 按钮和原子页面主题发布。Scene 离屏测试覆盖像素、命中、缩放、反向动画、设备重建。国际化检查 en-US/zh-TW 364/364 通过。完整编译及测试结果由父任务统一登记；真实 GUI 尚未运行。

## 最终集成验证

完整 ARM64 solution、独立 UInk 工程、UI3 Scene 离屏、100页CPU/GPU及存储生产测试、Draw3工作区回归、headless均通过。详见父任务 `validation.md`；既有UInk替换文件测试需在沙箱外执行，已通过。真实GUI未执行，保留人工验收。
