# 实施清单

- [x] 阅读prd和本层spec，确认文件所有权及接口。
- [x] 实现批准行为，保留编码换行；新增资源/编译文件告知主会话。
- [x] 添加生产逻辑驱动的无窗口回归，包含失败与内存约束。
- [x] 交主会话统一ARM64 solution构建，避免并发MSBuild。
- [x] 独立Trellis check审查并修复；记录局限及真实验证结果。

用户已审阅并以PLEASE IMPLEMENT THIS PLAN明确批准本prd/design对应方案。禁止commit/push。

## 实现检查点
- Draw3.WhiteboardModel.h 纯值主题 DTO；Bridge/Product/Host 按白板页代次及页码传递主题请求，运行快照发布主题、冷加载与保存失败状态。
- WhiteboardStore worker 以进程私有会话目录保存单个逻辑多页 .uink 的不可变版本；调用共享流式页文件 API，索引 durable 后才回执。队列最多两个待处理快照，同页合并；当前与一份备份以外的旧版本按强修订句柄验证清理。
- Controller 页级 revision/保存回执门控；成功离页释放 CPU 笔迹与历史、切页释放 GPU 缓存；冷加载完成才恢复目标页，读失败保持原页；正常退出逐页 drain，覆盖 parked 白板。
- v1 canvas extra 保存隐藏 stroke、visibility/redo、Clear 边界和 continuation 及主题；标准 content 仅保留可见笔迹。
- RunWhiteboardStorageProductionTest 使用生产 worker、100 个非空页、生产卸载 helper、实际读写失败/重试及完整历史往返。主会话负责 CLI 注册与统一 ARM64 编译执行。
- 当前 git diff --check 通过；尚未收到统一构建或运行测试结果，不宣称编译测试通过。无 GUI，无 commit/push。
- 后续独立检查重点：主题/页码 snapshot 发布一致性，退出故障 surfaced 行为，Clear continuation 深度与像素恢复，磁盘索引失败后的候选版本回收。

## 最终集成验证

完整 ARM64 solution、独立 UInk 工程、UI3 Scene 离屏、100页CPU/GPU及存储生产测试、Draw3工作区回归、headless均通过。详见父任务 `validation.md`；既有UInk替换文件测试需在沙箱外执行，已通过。真实GUI未执行，保留人工验收。
