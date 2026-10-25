# 笔速橡皮动态演示检查

## 结果

独立 trellis-check 对当前生产源码进行检查及最终复查，无待修复问题。覆盖预计算轨迹/遮罩、软件同款圆盘握持光标、实际像素合成、30Hz上限、静止/关闭/隐藏缓存、尺寸分档、共享上下文状态、设备epoch、可选资源降级、裁剪和多语言接线。

首次构建发现 Hide 暂停调用错误地访问局部快照才有的配置字段。已改为不可见分支不使用的固定参数；该分支立即暂停时钟并返回缓存，避免无关配置/主题读取。完整 solution 重新构建成功，最终修正经独立复查。

## 验证证据

- `InkeysRepo.sln Debug|ARM64`：原生 ARM64 MSBuild，完整构建退出0，0 errors。沿用PATH规范化和已有低并发配置。
- `InkeysHeadlessTests.exe --no-window --setting-eraser-demo-only`：退出0，settings=0、demo=0。真实ImGui夹具覆盖三语言/主题/DPI/宽窄、UV/裁剪/高度及原有控件；WARP检查实际播放shader像素、光标、背景、目标/邻字/大块清除、循环、禁用/暂停/缓存、状态恢复和替换SM4设备。
- 240次更新调用模拟1秒可见时间、10倍播放速率时实际绘制22次，含WARP完成等待耗时129.749ms；输出最大1536×432。停留/禁用重复调用不增加绘制，移动不重新创建管线/目标。
- 离线生成检查：精细目标擦除99.999%，邻字误擦笔速0%/固定22.06%；大块笔速100%/固定12.52%，上方保留笔记0%误擦。重复生成的两个PNG及帧表hash一致。
- 主代理和GPU实现代理静态查看实际离屏PNG；浅白/深绿、平滑手写化学结构、光标大小和擦除结果符合设计。预览位于 `research/eraser-demo-visuals/`。
- i18n：en-US、zh-TW均518/518；git diff检查通过。既有BOM/换行保持，预有CSO、PptCOM产物、CI workflow及既有图片SHA256未变化。

## 边界

未启动GUI/Computer Use或Win7实机。WARP/ImGui夹具及编译不等同实际HWND交互验收；SM4设备测试也不等同Win7系统测试。粗细变化为预计算教学演示数据，未改变或每帧运行真实笔速算法。性能数值为无窗口工作负载，不作为GUI帧率承诺。

构建时vcpkg首次查找pwsh.exe失败，已有流程成功回退Windows PowerShell；不构成构建失败，未修改工具链或项目配置规避环境问题。

不commit/push，父Trellis任务保持in_progress。
